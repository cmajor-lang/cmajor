/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "render.h"
#include "../gvc/gvio.h"
#include "../cgraph/strcasecmp.h"

static int N_EPSF_files;
static Dict_t *EPSF_contents;

static void ps_image_free(Dict_t * dict, usershape_t * p, Dtdisc_t * disc)
{
    (void)dict;
    (void)disc;
    free(p->data);
}

static Dtdisc_t ImageDictDisc2 = {};

struct Initialiser12
{
    Initialiser12()
    {
        ImageDictDisc2.key = offsetof(usershape_t, name);
        ImageDictDisc2.size = -1;
        ImageDictDisc2.freef = (Dtfree_f)ps_image_free;
    }
};

static Initialiser7 initialiser12;

static usershape_t *user_init(const char *str)
{
    char *contents;
    char line[BUFSIZ];
    FILE *fp;
    struct stat statbuf;
    bool must_inline;
    int lx, ly, ux, uy;
    usershape_t *us;

    if (!EPSF_contents)
	EPSF_contents = dtopen(&ImageDictDisc2, Dtoset);

    us = (usershape_t*) dtmatch(EPSF_contents, str);
    if (us)
	return us;

    if (!(fp = fopen(str, "r"))) {
	agerr(AGWARN, "couldn't open epsf file %s\n", str);
	return NULL;
    }
    /* try to find size */
    bool saw_bb = false;
    must_inline = false;
    while (fgets(line, sizeof(line), fp)) {
	if (sscanf
	    (line, "%%%%BoundingBox: %d %d %d %d", &lx, &ly, &ux, &uy) == 4) {
	    saw_bb = true;
	}
	if ((line[0] != '%') && strstr(line,"read")) must_inline = true;
	if (saw_bb && must_inline) break;
    }

    if (saw_bb) {
	us = GNEW(usershape_t);
	us->x = lx;
	us->y = ly;
	us->w = ux - lx;
	us->h = uy - ly;
	us->name = str;
	us->macro_id = N_EPSF_files++;
	fstat(fileno(fp), &statbuf);
	us->data = N_GNEW((size_t)statbuf.st_size + 1, char);
    contents = (char*) us->data;
	fseek(fp, 0, SEEK_SET);
	size_t rc = fread(contents, (size_t)statbuf.st_size, 1, fp);
	if (rc == 1) {
            contents[statbuf.st_size] = '\0';
            dtinsert(EPSF_contents, us);
            us->must_inline = must_inline;
        }
        else {
            agerr(AGWARN, "couldn't read from epsf file %s\n", str);
            free(us->data);
            free(us);
            us = NULL;
        }
    } else {
	agerr(AGWARN, "BoundingBox not found in epsf file %s\n", str);
	us = NULL;
    }
    fclose(fp);
    return us;
}

void epsf_init(node_t * n)
{
    epsf_t *desc;
    const char *str;
    usershape_t *us;
    int dx, dy;

    if ((str = safefile(agget(n, "shapefile")))) {
	us = user_init(str);
	if (!us)
	    return;
	dx = us->w;
	dy = us->h;
	ND_width(n) = PS2INCH(dx);
	ND_height(n) = PS2INCH(dy);
	ND_shape_info(n) = desc = NEW(epsf_t);
	desc->macro_id = us->macro_id;
	desc->offset.x = -us->x - (dx) / 2;
	desc->offset.y = -us->y - (dy) / 2;
    } else
	agerr(AGWARN, "shapefile not set or not found for epsf node %s\n", agnameof(n));
}

void epsf_free(node_t * n)
{

    free(ND_shape_info(n));
}


/* cat_libfile:
 * Write library files onto the given file pointer.
 * arglib is an NULL-terminated array of char*
 * Each non-trivial entry should be the name of a file to be included.
 * stdlib is an NULL-terminated array of char*
 * Each of these is a line of a standard library to be included.
 * If any item in arglib is the empty string, the stdlib is not used.
 * The stdlib is printed first, if used, followed by the user libraries.
 * We check that for web-safe file usage.
 */
void cat_libfile(GVJ_t * job, const char **arglib, const char **stdlib)
{
    FILE *fp;
    const char **s, *bp, *p;
    int i;
    bool use_stdlib = true;

    /* check for empty string to turn off stdlib */
    if (arglib) {
        for (i = 0; use_stdlib && ((p = arglib[i])); i++) {
            if (*p == '\0')
                use_stdlib = false;
        }
    }
    if (use_stdlib)
        for (s = stdlib; *s; s++) {
            gvputs(job, *s);
            gvputs(job, "\n");
        }
    if (arglib) {
        for (i = 0; (p = arglib[i]) != 0; i++) {
            if (*p == '\0')
                continue;       /* ignore empty string */
            const char *safepath = safefile(p);    /* make sure filename is okay */
	    if (!safepath) {
		agerr(AGWARN, "can't find library file %s\n", p);
	    }
            else if ((fp = fopen(safepath, "r"))) {
                while ((bp = Fgets(fp)))
                    gvputs(job, bp);
                gvputs(job, "\n"); /* append a newline just in case */
		fclose (fp);
            } else
                agerr(AGWARN, "can't open library file %s\n", safepath);
        }
    }
}

#define FILTER_EPSF 1
#ifdef FILTER_EPSF
/* this removes EPSF DSC comments that, when nested in another
 * document, cause errors in Ghostview and other Postscript
 * processors (although legal according to the Adobe EPSF spec).
 *
 * N.B. PostScript lines can end with \n, \r or \r\n.
 */
void epsf_emit_body(GVJ_t *job, usershape_t *us)
{
    char *p = (char*) us->data;
    while (*p) {
	/* skip %%EOF lines */
	if (!strncasecmp(p, "%%EOF", 5) || !strncasecmp(p, "%%BEGIN", 7) ||
	    !strncasecmp(p, "%%END", 5) || !strncasecmp(p, "%%TRAILER", 9)) {
	    /* check for *p since last line might not end in '\n' */
	    while (*p != '\0' && *p != '\r' && *p != '\n') p++;
	    if (*p == '\r' && p[1] == '\n') p += 2;
	    else if (*p) p++;
	    continue;
	}
	/* output line */
	while (*p != '\0' && *p != '\r' && *p != '\n') {
	    gvputc(job, *p);
	    p++;
	}
	if (*p == '\r' && p[1] == '\n') p += 2;
	else if (*p) p++;
	gvputc(job, '\n');
    }
}
#else
void epsf_emit_body(GVJ_t *job, usershape_t *us)
{
	gvputs(job, us->data);
}
#endif

void epsf_define(GVJ_t *job)
{
    usershape_t *us;

    if (!EPSF_contents)
	return;
    for (us = (usershape_t*) dtfirst(EPSF_contents); us; us = (usershape_t*)dtnext(EPSF_contents, us)) {
	if (us->must_inline)
	    continue;
	gvprintf(job, "/user_shape_%d {\n", us->macro_id);
	gvputs(job, "%%BeginDocument:\n");
	epsf_emit_body(job, us);
	gvputs(job, "%%EndDocument\n");
	gvputs(job, "} bind def\n");
    }
}

enum {ASCII, LATIN1, NONLATIN};

/* charsetOf:
 * Assuming legal utf-8 input, determine if
 * the character value range is ascii, latin-1 or otherwise.
 */
static int
charsetOf (char* s)
{
    int r = ASCII;
    unsigned char c;

    while ((c = *(unsigned char*)s++)) {
	if (c < 0x7F)
	    continue;
	else if ((c & 0xFC) == 0xC0) {
	    r = LATIN1;
	    s++; /* eat second byte */
	}
	else return NONLATIN;
    }
    return r;
}

/* ps_string:
 * internally, strings are always utf8. If chset is CHAR_LATIN1, we know
 * all of the values can be represented by latin-1; if chset is
 * CHAR_UTF8, we use the string as is; otherwise, we test to see if the
 * string is ascii, latin-1 or non-latin, and translate to latin-l if
 * possible.
 */
char *ps_string(char *ins, int chset)
{
    char *s;
    char *base;
    static agxbuf  xb;
    static int warned;

    switch (chset) {
    case CHAR_UTF8 :
        base = ins;
	break;
    case CHAR_LATIN1 :
        base = utf8ToLatin1 (ins);
	break;
    default :
	switch (charsetOf (ins)) {
	case ASCII :
	    base = ins;
	    break;
	case LATIN1 :
	    base = utf8ToLatin1 (ins);
	    break;
	case NONLATIN :
	    if (!warned) {
		agerr (AGWARN, "UTF-8 input uses non-Latin1 characters which cannot be handled by this PostScript driver\n");
		warned = 1;
	    }
	    base = ins;
	    break;
	default:
	    base = ins;
	    break;
	}
    }

    agxbputc (&xb, LPAREN);
    s = base;
    while (*s) {
        if ((*s == LPAREN) || (*s == RPAREN) || (*s == '\\'))
            agxbputc (&xb, '\\');
        agxbputc (&xb, *s++);
    }
    agxbputc (&xb, RPAREN);
    if (base != ins) free (base);
    s = agxbuse(&xb);
    return s;
}
