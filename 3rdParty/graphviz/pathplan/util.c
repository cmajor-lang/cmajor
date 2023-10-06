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
#include "pathutil.h"

void freePath(Ppolyline_t* p)
{
    free(p->ps);
    free(p);
}

int Ppolybarriers(Ppoly_t ** polys, int npolys, Pedge_t ** barriers,
		  int *n_barriers)
{
    Ppoly_t pp;
    int i, j, k, n, b;
    Pedge_t *bar;

    n = 0;
    for (i = 0; i < npolys; i++)
	n = n + polys[i]->pn;

    bar = (Pedge_t*) malloc(n * sizeof(Pedge_t));

    b = 0;
    for (i = 0; i < npolys; i++) {
	pp = *polys[i];
	for (j = 0; j < pp.pn; j++) {
	    k = j + 1;
	    if (k >= pp.pn)
		k = 0;
	    bar[b].a = pp.ps[j];
	    bar[b].b = pp.ps[k];
	    b++;
	}
    }
    assert(b == n);
    *barriers = bar;
    *n_barriers = n;
    return 1;
}

/* make_polyline:
 */
void
make_polyline(Ppolyline_t line, Ppolyline_t* sline)
{
    static int isz = 0;
    static Ppoint_t* ispline = 0;
    int i, j;
    int npts = 4 + 3*(line.pn-2);

    if (npts > isz) {
	ispline = (Ppoint_t*)gv_recalloc(ispline, (size_t)isz, (size_t)npts, sizeof(Ppoint_t));
	isz = npts;
    }

    j = i = 0;
    ispline[j+1] = ispline[j] = line.ps[i];
    j += 2;
    i++;
    for (; i < line.pn-1; i++) {
	ispline[j+2] = ispline[j+1] = ispline[j] = line.ps[i];
	j += 3;
    }
    ispline[j+1] = ispline[j] = line.ps[i];

    sline->pn = npts;
    sline->ps = ispline;
}

/**
 * @dir lib/pathplan
 * @brief finds and smooths shortest paths, API pathplan.h
 */
