/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#pragma once

#include "vispath.h"
#include "pathutil.h"

    typedef COORD **array2;

#define EQ(p,q)		((p.x == q.x) && (p.y == q.y))

    struct vconfig_s {
	int Npoly;
	int N;			/* number of points in walk of barriers */
	Ppoint_t *P;		/* barrier points */
	int *start;
	int *next;
	int *prev;

	/* this is computed from the above */
	array2 vis;
    };
#ifdef GVDLL
#ifdef PATHPLAN_EXPORTS
#define VIS_API
#else
#define VIS_API
#endif
#endif

#ifndef VIS_API
#define VIS_API /* nothing */
#endif

	VIS_API COORD *ptVis(vconfig_t *, int, Ppoint_t);
    VIS_API bool directVis(Ppoint_t, int, Ppoint_t, int, vconfig_t *);
    VIS_API void visibility(vconfig_t *);
    VIS_API int *makePath(Ppoint_t p, int pp, COORD * pvis,
			 Ppoint_t q, int qp, COORD * qvis,
			 vconfig_t * conf);

#undef VIS_API

