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

#include "pathgeom.h"

#ifdef GVDLL
#ifdef PATHPLAN_EXPORTS
#define VISPATH_API
#else
#define VISPATH_API
#endif
#endif

#ifndef VISPATH_API
#define VISPATH_API /* nothing */
#endif

/* open a visibility graph
 * Points in polygonal obstacles must be in clockwise order.
 */
    VISPATH_API vconfig_t *Pobsopen(Ppoly_t ** obstacles, int n_obstacles);

/* close a visibility graph, freeing its storage */
    VISPATH_API void Pobsclose(vconfig_t * config);

/* route a polyline from p0 to p1, avoiding obstacles.
 * if an endpoint is inside an obstacle, pass the polygon's index >=0
 * if the endpoint is not inside an obstacle, pass POLYID_NONE
 * if the endpoint location is not known, pass POLYID_UNKNOWN
 */

    VISPATH_API int Pobspath(vconfig_t * config, Ppoint_t p0, int poly0,
            Ppoint_t p1, int poly1,
            Ppolyline_t * output_route);

#define POLYID_NONE        -1111
#define POLYID_UNKNOWN    -2222

#undef VISPATH_API
