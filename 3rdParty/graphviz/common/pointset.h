/**
 * @file
 * @brief @ref point containers @ref PointSet and @ref PointMap
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

#include "../cdt/cdt.h"
#include "geom.h"

    typedef Dict_t PointSet; ///< set of @ref point. Created by @ref newPS
    typedef Dict_t PointMap; ///< map of @ref point. Created by @ref newPM

#ifdef GVDLL
#ifdef GVC_EXPORTS
#define POINTSET_API
#else
#define POINTSET_API
#endif
#endif

/// @cond
#ifndef POINTSET_API
#define POINTSET_API /* nothing */
#endif
/// @endcond

	POINTSET_API PointSet *newPS(void);
    POINTSET_API void freePS(PointSet *);
    POINTSET_API void insertPS(PointSet *, point);
    POINTSET_API void addPS(PointSet *, int x, int y);
    POINTSET_API int inPS(PointSet *, point);
    POINTSET_API int isInPS(PointSet *, int x, int y);
    POINTSET_API int sizeOf(PointSet *);
    POINTSET_API point *pointsOf(PointSet *);

    POINTSET_API PointMap *newPM(void);
    POINTSET_API void clearPM(PointMap *);
    POINTSET_API void freePM(PointMap *);
    POINTSET_API int insertPM(PointMap *, int x, int y, int value);

#undef POINTSET_API
