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

/* memory management discipline and entry points */
static void *memopen(Agdisc_t* disc)
{
    (void)disc; /* unused */
    return NULL;
}

static void *memalloc(void *heap, size_t request)
{
    void *rv;

    (void)heap;
    rv = calloc(1, request);
    return rv;
}

static void *memresize(void *heap, void *ptr, size_t oldsize,
		       size_t request)
{
    void *rv;

    (void)heap;
    rv = realloc(ptr, request);
    if (rv != NULL && request > oldsize)
	memset((char *) rv + oldsize, 0, request - oldsize);
    return rv;
}

static void memfree(void *heap, void *ptr)
{
    (void)heap;
    free(ptr);
}

inline Agmemdisc_t AgMemDisc =
    { memopen, memalloc, memresize, memfree, NULL };

inline void *agalloc(Agraph_t * g, size_t size)
{
    void *mem;

    mem = AGDISC(g, mem)->alloc(AGCLOS(g, mem), size);
    if (mem == NULL)
	 agerr(AGERR,"memory allocation failure");
    return mem;
}

inline void *agrealloc(Agraph_t * g, void *ptr, size_t oldsize, size_t size)
{
    void *mem;

    if (size > 0) {
	if (ptr == 0)
	    mem = agalloc(g, size);
	else
	    mem =
		AGDISC(g, mem)->resize(AGCLOS(g, mem), ptr, oldsize, size);
	if (mem == NULL)
	     agerr(AGERR,"memory re-allocation failure");
    } else
	mem = NULL;
    return mem;
}

inline void agfree(Agraph_t * g, void *ptr)
{
    if (ptr)
	(AGDISC(g, mem)->free) (AGCLOS(g, mem), ptr);
}
