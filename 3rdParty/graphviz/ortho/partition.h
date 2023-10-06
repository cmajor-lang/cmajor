/**
 * @file
 * @brief function @ref partition, subroutine of @ref mkMaze
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

#include "maze.h"

/**
 * @brief partitions space around cells (nodes) into rectangular tiles
 * @param[in] cells rectangular borders of user's input graph's nodes
 * @param[in] bb range of the space to partition
 * @param[out] nrects number of tiles
 * @returns array of the tiles
 */

boxf *partition(cell *cells, int ncells, size_t *nrects, boxf bb);
