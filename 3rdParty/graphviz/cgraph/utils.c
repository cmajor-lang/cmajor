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

static Agraph_t *Ag_dictop_G;

/* only indirect call through dtopen() is expected */
void *agdictobjmem(Dict_t * dict, void * p, size_t size, Dtdisc_t * disc)
{
    Agraph_t *g;

    (void)dict;
    (void)disc;
    g = Ag_dictop_G;
    if (g) {
	if (p)
	    agfree(g, p);
	else
	    return agalloc(g, size);
    } else {
	if (p)
	    free(p);
	else
	    return malloc(size);
    }
    return NULL;
}

void agdictobjfree(Dict_t * dict, void * p, Dtdisc_t * disc)
{
    Agraph_t *g;

    (void)dict;
    (void)disc;
    g = Ag_dictop_G;
    if (g)
	agfree(g, p);
    else
	free(p);
}

Dict_t *agdtopen(Agraph_t * g, Dtdisc_t * disc, Dtmethod_t * method)
{
    Dtmemory_f memf;
    Dict_t *d;

    memf = disc->memoryf;
    disc->memoryf = agdictobjmem;
    Ag_dictop_G = g;
    d = dtopen(disc, method);
    disc->memoryf = memf;
    Ag_dictop_G = NULL;
    return d;
}

int agdtdelete(Agraph_t * g, Dict_t * dict, void *obj)
{
    Ag_dictop_G = g;
    return dtdelete(dict, obj) != NULL;
}

int agdtclose(Agraph_t * g, Dict_t * dict)
{
    Dtmemory_f memf;
    Dtdisc_t *disc;

    disc = dtdisc(dict, NULL, 0);
    memf = disc->memoryf;
    disc->memoryf = agdictobjmem;
    Ag_dictop_G = g;
    if (dtclose(dict))
	return 1;
    disc->memoryf = memf;
    Ag_dictop_G = NULL;
    return 0;
}

void agdtdisc(Agraph_t * g, Dict_t * dict, Dtdisc_t * disc)
{
    (void)g; /* unused */
    if (disc && dtdisc(dict, NULL, 0) != disc) {
	dtdisc(dict, disc, 0);
    }
    /* else unchanged, disc is same as old disc */
}
