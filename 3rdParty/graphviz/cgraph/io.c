/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "cghdr.h"

static int iofread(void *chan, char *buf, int bufsize)
{
    if (fgets(buf, bufsize, (FILE*)chan))
	return (int)strlen(buf);
    else
	return 0;
}

/* default IO methods */
static int ioputstr(void *chan, const char *str)
{
    return fputs(str, (FILE*)chan);
}

static int ioflush(void *chan)
{
    return fflush((FILE*)chan);
}

Agiodisc_t AgIoDisc = { iofread, ioputstr, ioflush };

typedef struct {
    const char *data;
    size_t len;
    size_t cur;
} rdr_t;

static int
memiofread(void *chan, char *buf, int bufsize)
{
    const char *ptr;
    char *optr;
    char c;
    int l;
    rdr_t *s;

    if (bufsize == 0) return 0;
    s = (rdr_t*)chan;
    if (s->cur >= s->len)
        return 0;
    l = 0;
    ptr = s->data + s->cur;
    optr = buf;
    /* We know we have at least one character */
    c = *ptr++;
    do {
        *optr++ = c;
        l++;
	/* continue if c is not newline, we have space in buffer,
	 * and next character is non-null (we are working with
	 * null-terminated strings.
	 */
    } while (c != '\n' && l < bufsize && (c = *ptr++));
    s->cur += (size_t)l;
    return l;
}

static Agiodisc_t memIoDisc = {memiofread, 0, 0};

static Agraph_t *agmemread0(Agraph_t *arg_g, const char *cp)
{
    Agraph_t* g;
    rdr_t rdr;
    Agdisc_t disc;

    memIoDisc.putstr = AgIoDisc.putstr;
    memIoDisc.flush = AgIoDisc.flush;
    rdr.data = cp;
    rdr.len = strlen(cp);
    rdr.cur = 0;

    disc.mem = &AgMemDisc;
    disc.id = &AgIdDisc;
    disc.io = &memIoDisc;
    if (arg_g) g = agconcat(arg_g, &rdr, &disc);
    else g = agread (&rdr, &disc);
    /* Null out filename and reset line number
     * The name may have been set with a ppDirective, and
     * we want to reset line_num.
     */
    agsetfile(NULL);
    return g;
}

Agraph_t *agmemread(const char *cp)
{
    return agmemread0(0, cp);
}

Agraph_t *agmemconcat(Agraph_t *g, const char *cp)
{
    return agmemread0(g, cp);
}
