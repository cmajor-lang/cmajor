/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "../cgraph/alloc.h"
#include "../cgraph/cghdr.h"

#define MAX(a,b)	((a)>(b)?(a):(b))
static agerrlevel_t agerrno;		/* Last error level */
static agerrlevel_t agerrlevel = AGWARN;	/* Report errors >= agerrlevel */
static int agmaxerr;

static long aglast;		/* Last message */
static FILE *agerrout;		/* Message file */
static agusererrf usererrf;     /* User-set error function */

agusererrf
agseterrf (agusererrf newf)
{
    agusererrf oldf = usererrf;
    usererrf = newf;
    return oldf;
}

agerrlevel_t agseterr(agerrlevel_t lvl)
{
    agerrlevel_t oldv = agerrlevel;
    agerrlevel = lvl;
    return oldv;
}

char *aglasterr()
{
    if (!agerrout)
	return 0;
    fflush(agerrout);
    long endpos = ftell(agerrout);
    size_t len = (size_t)(endpos - aglast);
    char *buf = (char*) gv_alloc(len + 1);
    fseek(agerrout, aglast, SEEK_SET);
    len = fread(buf, sizeof(char), len, agerrout);
    buf[len] = '\0';
    fseek(agerrout, endpos, SEEK_SET);

    return buf;
}

/* userout:
 * Report messages using a user-supplied write function
 */
static void
userout (agerrlevel_t level, const char *fmt, va_list args)
{
    // find out how much space we need to construct this string
    size_t bufsz;
    {
	va_list args2;
	va_copy(args2, args);
	int rc = vsnprintf(NULL, 0, fmt, args2);
	va_end(args2);
	if (rc < 0) {
	    va_end(args);
	    fprintf(stderr, "%s: vsnprintf failure\n", __func__);
	    return;
	}
	bufsz = (size_t)rc + 1; // account for NUL terminator
    }

    // allocate a buffer for the string
    char *buf = (char*) malloc(bufsz);
    if (buf == NULL) {
	va_end(args);
	fprintf(stderr, "%s: could not allocate memory\n", __func__);
	return;
    }

    if (level != AGPREV) {
	usererrf ((char*) ((level == AGERR) ? "Error" : "Warning"));
	usererrf ((char*) ": ");
    }

    // construct the full error in our buffer
    int rc = vsnprintf(buf, bufsz, fmt, args);
    va_end(args);
    if (rc < 0) {
	free(buf);
	fprintf(stderr, "%s: vsnprintf failure\n", __func__);
	return;
    }

    // yield our constructed error
    (void)usererrf(buf);

    free(buf);
}

static int agerr_va(agerrlevel_t level, const char *fmt, va_list args)
{
    agerrlevel_t lvl;

    /* Use previous error level if continuation message;
     * Convert AGMAX to AGERROR;
     * else use input level
     */
    lvl = (level == AGPREV ? agerrno : (level == AGMAX) ? AGERR : level);

    /* store this error level */
    agerrno = lvl;
    agmaxerr = MAX(agmaxerr, (int)agerrno);

    /* We report all messages whose level is bigger than the user set agerrlevel
     * Setting agerrlevel to AGMAX turns off immediate error reporting.
     */
    if (lvl >= agerrlevel) {
	if (usererrf)
	    userout (level, fmt, args);
	else {
	    if (level != AGPREV)
		fprintf(stderr, "%s: ", (level == AGERR) ? "Error" : "Warning");
	    vfprintf(stderr, fmt, args);
	    va_end(args);
	}
	return 0;
    }

    if (!agerrout) {
	agerrout = tmpfile();
	if (!agerrout)
	    return 1;
    }

    if (level != AGPREV)
	aglast = ftell(agerrout);
    vfprintf(agerrout, fmt, args);
    return 0;
}

int agerr(agerrlevel_t level, const char *fmt, ...)
{
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = agerr_va(level, fmt, args);
    va_end(args);
    return ret;
}

void agerrorf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    agerr_va(AGERR, fmt, args);
    va_end(args);
}

void agwarningf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    agerr_va(AGWARN, fmt, args);
    va_end(args);
}

int agerrors() { return agmaxerr; }

int agreseterrors()
{
    int rc = agmaxerr;
    agmaxerr = 0;
    return rc;
}

