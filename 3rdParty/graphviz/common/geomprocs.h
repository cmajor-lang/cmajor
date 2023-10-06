/**
 * @file
 * @brief geometric functions (e.g. on points and boxes)
 *
 * with application to, but no specific dependence on graphs
 */

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

#include "geom.h"

#ifdef GVDLL
#ifdef GVC_EXPORTS
#define GEOMPROCS_API
#else
#define GEOMPROCS_API extern
#endif
#endif

#ifndef GEOMPROCS_API
#define GEOMPROCS_API /* nothing */
#endif

GEOMPROCS_API boxf flip_rec_boxf(boxf b, pointf p);

GEOMPROCS_API double ptToLine2 (pointf l1, pointf l2, pointf p);

GEOMPROCS_API int lineToBox(pointf p1, pointf p2, boxf b);

GEOMPROCS_API point ccwrotatep(point p, int ccwrot);
GEOMPROCS_API pointf ccwrotatepf(pointf p, int ccwrot);

GEOMPROCS_API point cwrotatep(point p, int cwrot);
GEOMPROCS_API pointf cwrotatepf(pointf p, int cwrot);

GEOMPROCS_API void rect2poly(pointf *p);

GEOMPROCS_API int line_intersect (pointf a, pointf b, pointf c, pointf d, pointf* p);

static inline point add_point(point p, point q)
{
    point r;

    r.x = p.x + q.x;
    r.y = p.y + q.y;
    return r;
}

static inline pointf add_pointf(pointf p, pointf q)
{
    pointf r;

    r.x = p.x + q.x;
    r.y = p.y + q.y;
    return r;
}

static inline point sub_point(point p, point q)
{
    point r;

    r.x = p.x - q.x;
    r.y = p.y - q.y;
    return r;
}

static inline pointf sub_pointf(pointf p, pointf q)
{
    pointf r;

    r.x = p.x - q.x;
    r.y = p.y - q.y;
    return r;
}

static inline pointf mid_pointf(pointf p, pointf q)
{
    pointf r;

    r.x = (p.x + q.x) / 2.;
    r.y = (p.y + q.y) / 2.;
    return r;
}

static inline pointf interpolate_pointf(double t, pointf p, pointf q)
{
    pointf r;

    r.x = p.x + t * (q.x - p.x);
    r.y = p.y + t * (q.y - p.y);
    return r;
}

static inline point exch_xy(point p)
{
    point r;

    r.x = p.y;
    r.y = p.x;
    return r;
}

static inline pointf exch_xyf(pointf p)
{
    pointf r;

    r.x = p.y;
    r.y = p.x;
    return r;
}

static inline int boxf_overlap(boxf b0, boxf b1)
{
    return OVERLAP(b0, b1);
}

static inline pointf perp (pointf p)
{
    pointf r;

    r.x = -p.y;
    r.y = p.x;
    return r;
}

static inline pointf scale (double c, pointf p)
{
    pointf r;

    r.x = c * p.x;
    r.y = c * p.y;
    return r;
}

#undef GEOMPROCS_API
