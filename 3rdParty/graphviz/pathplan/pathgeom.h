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

#ifdef GVDLL
#ifdef PATHPLAN_EXPORTS
#define PATHGEOM_API
#else
#define PATHGEOM_API
#endif
#endif

#ifndef PATHGEOM_API
#define PATHGEOM_API /* nothing */
#endif

#ifdef HAVE_POINTF_S
    typedef struct pointf_s Ppoint_t;
    typedef struct pointf_s Pvector_t;
#else
    typedef struct Pxy_t {
	double x, y;
    } Pxy_t;

    typedef struct Pxy_t Ppoint_t;
    typedef struct Pxy_t Pvector_t;
#endif

    typedef struct Ppoly_t {
	Ppoint_t *ps;
	int pn;
    } Ppoly_t;

    typedef Ppoly_t Ppolyline_t;

    typedef struct Pedge_t {
	Ppoint_t a, b;
    } Pedge_t;

/* opaque state handle for visibility graph operations */
    typedef struct vconfig_s vconfig_t;

    PATHGEOM_API void freePath(Ppolyline_t* p);

#undef PATHGEOM_API
