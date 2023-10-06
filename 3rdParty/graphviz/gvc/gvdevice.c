/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

/*
 *  This library forms the socket for run-time loadable device plugins.
 */

#include "config.h"

#ifdef HAVE_LIBZ
#include "../zlib.h"

#ifndef OS_CODE
#  define OS_CODE  0x03  /* assume Unix */
#endif
static const unsigned char z_file_header[] =
   {0x1f, 0x8b, /*magic*/ Z_DEFLATED, 0 /*flags*/, 0,0,0,0 /*time*/, 0 /*xflags*/, OS_CODE};

static z_stream z_strm;
static unsigned char *df;
static unsigned int dfallocated;
static uint64_t crc;
#endif /* HAVE_LIBZ */

#include "../cgraph/exit.h"
#include "../common/const.h"
#include "../common/memory.h"
#include "../gvc/gvplugin_device.h"
#include "../gvc/gvcjob.h"
#include "../gvc/gvcint.h"
#include "../gvc/gvcproc.h"
#include "../common/utils.h"
#include "../gvc/gvio.h"

static const int PAGE_ALIGN = 4095;		/* align to a 4K boundary (less one), typical for Linux, Mac OS X and Windows memory allocation */

static size_t gvwrite_no_z(GVJ_t * job, const void *s, size_t len) {
    if (job->gvc->write_fn)   /* externally provided write discipline */
	return job->gvc->write_fn(job, (const char*) s, len);
    if (job->output_data) {
	if (len > job->output_data_allocated - (job->output_data_position + 1)) {
	    /* ensure enough allocation for string = null terminator */
	    job->output_data_allocated = (job->output_data_position + len + 1 + PAGE_ALIGN) & ~PAGE_ALIGN;
	    job->output_data = (char*) realloc(job->output_data, job->output_data_allocated);
	    if (!job->output_data) {
                job->common->errorfn("memory allocation failure\n");
		graphviz_exit(1);
	    }
	}
	memcpy(job->output_data + job->output_data_position, s, len);
        job->output_data_position += len;
	job->output_data[job->output_data_position] = '\0'; /* keep null terminated */
	return len;
    }
    else {
        assert(job->output_file != NULL);
        return fwrite(s, sizeof(char), len, job->output_file);
    }
    return 0;
}

static void auto_output_filename(GVJ_t *job)
{
    static char *buf;
    static size_t bufsz;
    char gidx[100];  /* large enough for '.' plus any integer */
    char *fn;
    size_t len;

    if (job->graph_index)
        snprintf(gidx, sizeof(gidx), ".%d", job->graph_index + 1);
    else
        gidx[0] = '\0';
    if (!(fn = job->input_filename))
        fn = "noname.gv";
    len = strlen(fn)                    /* typically "something.gv" */
        + strlen(gidx)                  /* "", ".2", ".3", ".4", ... */
        + 1                             /* "." */
        + strlen(job->output_langname)  /* e.g. "png" */
        + 1;                            /* null terminator */
    if (bufsz < len) {
            bufsz = len + 10;
            buf = (char*) realloc(buf, bufsz * sizeof(char));
    }
    strcpy(buf, fn);
    strcat(buf, gidx);
    strcat(buf, ".");

    {
        char *dst = buf + strlen(buf);
        const char *src = job->output_langname;
        const char *src_end = src + strlen(src);
        for (const char *q = src_end; ; --q) {
            if (*q == ':') {
                dst += sprintf(dst, "%.*s.", (int)(src_end - q - 1), q + 1);
                src_end = q;
            }
            if (q == src) {
                sprintf(dst, "%.*s", (int)(src_end - src), src);
                break;
            }
        }
    }

    job->output_filename = buf;
}

/* gvdevice_initialize:
 * Return 0 on success, non-zero on failure
 */
int gvdevice_initialize(GVJ_t * job)
{
    gvdevice_engine_t *gvde = job->device.engine;
    GVC_t *gvc = job->gvc;

    if (gvde && gvde->initialize) {
	gvde->initialize(job);
    }
    else if (job->output_data) {
    }
    /* if the device has no initialization then it uses file output */
    else if (!job->output_file) {        /* if not yet opened */
        if (gvc->common.auto_outfile_names)
            auto_output_filename(job);
        if (job->output_filename) {
            job->output_file = fopen(job->output_filename, "w");
            if (job->output_file == NULL) {
		job->common->errorfn("Could not open \"%s\" for writing : %s\n",
		    job->output_filename, strerror(errno));
                /* perror(job->output_filename); */
                return 1;
            }
        }
        else
            job->output_file = stdout;

#ifdef HAVE_SETMODE
#ifdef O_BINARY
        if (job->flags & GVDEVICE_BINARY_FORMAT)
#ifdef _WIN32
		_setmode(fileno(job->output_file), O_BINARY);
#else
		setmode(fileno(job->output_file), O_BINARY);
#endif
#endif
#endif
    }

    if (job->flags & GVDEVICE_COMPRESSED_FORMAT) {
#ifdef HAVE_LIBZ
	z_stream *z = &z_strm;

	z->zalloc = 0;
	z->zfree = 0;
	z->opaque = 0;
	z->next_in = NULL;
	z->next_out = NULL;
	z->avail_in = 0;

	gvwrite_no_z(job, z_file_header, sizeof(z_file_header));
#else
	job->common->errorfn("No libz support.\n");
	return 1;
#endif
    }
    return 0;
}

size_t gvwrite (GVJ_t * job, const char *s, size_t len)
{
    size_t ret, olen;

    if (!len || !s)
	return 0;

    if (job->flags & GVDEVICE_COMPRESSED_FORMAT) {
#ifdef HAVE_LIBZ
	z_streamp z = &z_strm;


	z->next_in = (unsigned char*)s;
	z->avail_in = len;
	while (z->avail_in) {
	    z->next_out = df;
	    z->avail_out = dfallocated;

	    if ((olen = z->next_out - df)) {
		ret = gvwrite_no_z(job, df, olen);
	        if (ret != olen) {
                    job->common->errorfn("gvwrite_no_z problem %d\n", ret);
	            graphviz_exit(1);
	        }
	    }
	}

#else
        (void)olen;
	job->common->errorfn("No libz support.\n");
	graphviz_exit(1);
#endif
    }
    else { /* uncompressed write */
	ret = gvwrite_no_z (job, s, len);
	if (ret != len) {
	    job->common->errorfn("gvwrite_no_z problem %d\n", len);
	    graphviz_exit(1);
	}
    }
    return len;
}

int gvferror (FILE* stream)
{
    GVJ_t *job = (GVJ_t*)stream;

    if (!job->gvc->write_fn && !job->output_data)
	return ferror(job->output_file);

    return 0;
}

int gvputs(GVJ_t * job, const char *s)
{
    size_t len = strlen(s);

    if (gvwrite (job, s, len) != len) {
	return EOF;
    }
    return 1;
}

int gvputs_xml(GVJ_t *job, const char *s) {
  xml_flags_t flags = {};
  flags.dash = 1;
  flags.nbsp = 1;
  return xml_escape(s, flags, (int (*)(void *, const char *))gvputs, job);
}

void gvputs_nonascii(GVJ_t *job, const char *s) {
  for (; *s != '\0'; ++s) {
    if (*s == '\\') {
      gvputs(job, "\\\\");
    } else if (isascii((int)*s)) {
      gvputc(job, *s);
    } else {
      gvprintf(job, "%03o", (unsigned)*s);
    }
  }
}

int gvputc(GVJ_t * job, int c)
{
    const char cc = c;

    if (gvwrite (job, &cc, 1) != 1) {
	return EOF;
    }
    return c;
}

int gvflush (GVJ_t * job)
{
    if (job->output_file
      && ! job->external_context
      && ! job->gvc->write_fn) {
	return fflush(job->output_file);
    }
    else
	return 0;
}

static void gvdevice_close(GVJ_t * job)
{
    if (job->output_filename
      && job->output_file != stdout
      && ! job->external_context) {
        if (job->output_file) {
            fclose(job->output_file);
            job->output_file = NULL;
        }
	job->output_filename = NULL;
    }
}

void gvdevice_format(GVJ_t * job)
{
    gvdevice_engine_t *gvde = job->device.engine;

    if (gvde && gvde->format)
	gvde->format(job);
    gvflush (job);
}

void gvdevice_finalize(GVJ_t * job)
{
    gvdevice_engine_t *gvde = job->device.engine;
    bool finalized_p = false;

    if (job->flags & GVDEVICE_COMPRESSED_FORMAT) {
#ifdef HAVE_LIBZ
	z_streamp z = &z_strm;
	unsigned char out[8] = "";
	int cnt = 0;

	z->next_in = out;
	z->avail_in = 0;
	z->next_out = df;
	z->avail_out = dfallocated;
	while (cnt++ <= 100) {
	    gvwrite_no_z(job, df, z->next_out - df);
	    z->next_out = df;
	    z->avail_out = dfallocated;
	}
	gvwrite_no_z(job, df, z->next_out - df);

	out[0] = (unsigned char)crc;
	out[1] = (unsigned char)(crc >> 8);
	out[2] = (unsigned char)(crc >> 16);
	out[3] = (unsigned char)(crc >> 24);
	out[4] = (unsigned char)z->total_in;
	out[5] = (unsigned char)(z->total_in >> 8);
	out[6] = (unsigned char)(z->total_in >> 16);
	out[7] = (unsigned char)(z->total_in >> 24);
	gvwrite_no_z(job, out, sizeof(out));
#else
	job->common->errorfn("No libz support\n");
	graphviz_exit(1);
#endif
    }

    if (gvde) {
	if (gvde->finalize) {
	    gvde->finalize(job);
	    finalized_p = true;
	}
    }

    if (! finalized_p) {
        /* if the device has no finalization then it uses file output */
	gvflush (job);
	gvdevice_close(job);
    }
}

void gvprintf(GVJ_t * job, const char *format, ...)
{
    char buf[BUFSIZ];
    int len;
    va_list argp;
    char* bp = buf;

    va_start(argp, format);
    {
	va_list argp2;
	va_copy(argp2, argp);
	len = vsnprintf(buf, BUFSIZ, format, argp2);
	va_end(argp2);
    }
    if (len < 0) {
	va_end(argp);
	agerr (AGERR, "gvprintf: %s\n", strerror(errno));
	return;
    }
    else if (len >= BUFSIZ) {
    /* C99 vsnprintf returns the length that would be required
     * to write the string without truncation.
     */
	bp = (char*) gmalloc((size_t)len + 1);
	len = vsprintf(bp, format, argp);
    }
    va_end(argp);

    gvwrite(job, bp, (size_t)len);
    if (bp != buf)
	free (bp);
}


/* Test with:
 *	cc -DGVPRINTNUM_TEST gvprintnum.c -o gvprintnum
 */

#define DECPLACES 4
#define DECPLACES_SCALE 10000

/* use macro so maxnegnum is stated just once for both double and string versions */
#define val_str(n, x) static double n = x; static char n##str[] = #x;
val_str(maxnegnum, -999999999999999.99)

/* we use len and don't need the string to be terminated */
/* #define TERMINATED_NUMBER_STRING */

/* Note.  Returned string is only good until the next call to gvprintnum */
static char * gvprintnum (size_t *len, double number)
{
    static char tmpbuf[sizeof(maxnegnumstr)];   /* buffer big enough for worst case */
    char *result = tmpbuf+sizeof(maxnegnumstr); /* init result to end of tmpbuf */
    long int N;
    bool showzeros, negative;
    int digit, i;

    /*
        number limited to a working range: maxnegnum >= n >= -maxnegnum
	N = number * DECPLACES_SCALE rounded towards zero,
	printing to buffer in reverse direction,
	printing "." after DECPLACES
	suppressing trailing "0" and "."
     */

    if (number < maxnegnum) {		/* -ve limit */
	*len = sizeof(maxnegnumstr)-1;  /* len doesn't include terminator */
	return maxnegnumstr;;
    }
    if (number > -maxnegnum) {		/* +ve limit */
	*len = sizeof(maxnegnumstr)-2;  /* len doesn't include terminator or sign */
	return maxnegnumstr+1;		/* +1 to skip the '-' sign */
    }
    number *= DECPLACES_SCALE;		/* scale by DECPLACES_SCALE */
    if (number < 0.0)			/* round towards zero */
        N = number - 0.5;
    else
        N = number + 0.5;
    if (N == 0) {			/* special case for exactly 0 */
	*len = 1;
	return "0";
    }
    if ((negative = N < 0))		/* avoid "-0" by testing rounded int */
        N = -N;				/* make number +ve */
#ifdef TERMINATED_NUMBER_STRING
    *--result = '\0';			/* terminate the result string */
#endif
    showzeros = false;			/* don't print trailing zeros */
    for (i = DECPLACES; N || i > 0; i--) {  /* non zero remainder,
						or still in fractional part */
        digit = (int)(N % 10L);			/* next least-significant digit */
        N /= 10;
        if (digit || showzeros) {	/* if digit is non-zero,
						or if we are printing zeros */
            *--result = (char)digit | '0';	/* convert digit to ascii */
            showzeros = true;		/* from now on we must print zeros */
        }
        if (i == 1) {			/* if completed fractional part */
            if (showzeros)		/* if there was a non-zero fraction */
                *--result = '.';	/* print decimal point */
            showzeros = true;		/* print all digits in int part */
        }
    }
    if (negative)			/* print "-" if needed */
        *--result = '-';
#ifdef TERMINATED_NUMBER_STRING
    *len = tmpbuf+sizeof(maxnegnumstr)-1 - result;
#else
    *len = tmpbuf+sizeof(maxnegnumstr) - result;
#endif
    return result;
}


#ifdef GVPRINTNUM_TEST
int main (int argc, char *argv[])
{
    char *buf;
    size_t len;

    double test[] = {
	-maxnegnum*1.1, -maxnegnum*.9,
	1e8, 10.008, 10, 1, .1, .01,
	.006, .005, .004, .001, 1e-8,
	0, -0,
	-1e-8, -.001, -.004, -.005, -.006,
	-.01, -.1, -1, -10, -10.008, -1e8,
	maxnegnum*.9, maxnegnum*1.1
    };
    int i = sizeof(test) / sizeof(test[0]);

    while (i--) {
	buf = gvprintnum(&len, test[i]);
        fprintf (stdout, "%g = %s %d\n", test[i], buf, len);
    }

    graphviz_exit(0);
}
#endif

/* gv_trim_zeros
* Identify Trailing zeros and decimal point, if possible.
* Assumes the input is the result of %.02f printing.
*/
static size_t gv_trim_zeros(const char *buf) {
  auto dotp = (char *) strchr(buf, '.');
  if (dotp == NULL) {
    return strlen(buf);
  }

  // check this really is the result of %.02f printing
  assert(isdigit((int)dotp[1]) && isdigit((int)dotp[2]) && dotp[3] == '\0');

  if (dotp[2] == '0') {
    if (dotp[1] == '0') {
      return (size_t)(dotp - buf);
    } else {
      return (size_t)(dotp - buf) + 2;
    }
  }

  return strlen(buf);
}

void gvprintdouble(GVJ_t * job, double num)
{
    // Prevents values like -0
    if (num > -0.005 && num < 0.005)
    {
        gvwrite(job, "0", 1);
        return;
    }

    char buf[50];

    snprintf(buf, 50, "%.02f", num);
    size_t len = gv_trim_zeros(buf);

    gvwrite(job, buf, len);
}

void gvprintpointf(GVJ_t * job, pointf p)
{
    char *buf;
    size_t len;

    buf = gvprintnum(&len, p.x);
    gvwrite(job, buf, len);
    gvwrite(job, " ", 1);
    buf = gvprintnum(&len, p.y);
    gvwrite(job, buf, len);
}

void gvprintpointflist(GVJ_t *job, pointf *p, size_t n) {
  const char *separator = "";
  for (size_t i = 0; i < n; ++i) {
    gvputs(job, separator);
    gvprintpointf(job, p[i]);
    separator = " ";
  }
}
