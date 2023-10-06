/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "../gvc/config.h"

#include "../cgraph/exit.h"
#include "../cgraph/likely.h"
#include "memory.h"

void *zmalloc(size_t nbytes)
{
    if (nbytes == 0)
	return 0;
    return gcalloc(1, nbytes);
}

void *zrealloc(void *ptr, size_t size, size_t elt, size_t osize)
{
    void *p = realloc(ptr, size * elt);
    if (UNLIKELY(p == NULL && size)) {
	fprintf(stderr, "out of memory\n");
	graphviz_exit(EXIT_FAILURE);
    }
    if (osize < size)
	memset((char *) p + (osize * elt), '\0', (size - osize) * elt);
    return p;
}

void *gcalloc(size_t nmemb, size_t size)
{
    char *rv = (char*)calloc(nmemb, size);
    if (UNLIKELY(nmemb > 0 && size > 0 && rv == NULL)) {
	fprintf(stderr, "out of memory\n");
	graphviz_exit(EXIT_FAILURE);
    }
    return rv;
}

void *gmalloc(size_t nbytes)
{
    char *rv;
    if (nbytes == 0)
	return NULL;
    rv = (char*) malloc(nbytes);
    if (UNLIKELY(rv == NULL)) {
	fprintf(stderr, "out of memory\n");
	graphviz_exit(EXIT_FAILURE);
    }
    return rv;
}

void *grealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (UNLIKELY(p == NULL && size)) {
	fprintf(stderr, "out of memory\n");
	graphviz_exit(EXIT_FAILURE);
    }
    return p;
}
