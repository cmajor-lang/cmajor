/**
 * @file
 * @brief makes @ref maze with @ref mkMaze for routing orthogonal edges
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

#include "sgraph.h"

enum {M_RIGHT=0, M_TOP, M_LEFT, M_BOTTOM};

#define MZ_ISNODE   1
#define MZ_VSCAN    2
#define MZ_HSCAN    4
#define MZ_SMALLV   8
#define MZ_SMALLH  16

  /// @brief cell corresponds to node
#define IsNode(cp) (cp->flags & MZ_ISNODE)  
  /// @brief cell already inserted in vertical channel
#define IsVScan(cp) (cp->flags & MZ_VSCAN)  
  /// @brief cell already inserted in horizontal channel
#define IsHScan(cp) (cp->flags & MZ_HSCAN)
  /// @brief cell has small height corresponding to a small height node
#define IsSmallV(cp) (cp->flags & MZ_SMALLV)
  /// @brief cell has small width corresponding to a small width node
#define IsSmallH(cp) (cp->flags & MZ_SMALLH)

/// @brief result of partitioning available space, part of @ref maze

typedef struct cell {
  int flags;
  int nedges;
  sedge* edges[6]; /**< @brief up to six links (@ref sedge) between
                        four @ref sides (@ref snode) of the cell
                            1. ┘ left — top
                            2. └ top — right
                            3. ┐ left — bottom
                            4. ┌ bottom — right
                            5. │ top — bottom
                            6. ─ left — right
                    */
  int nsides;
  snode** sides; ///< @brief up to four sides: @ref M_RIGHT, @ref M_TOP, @ref M_LEFT, @ref M_BOTTOM
  boxf  bb;
} cell;

/**
 * @struct maze
 * @brief available channels for orthogonal edges around nodes of @ref graph_t
 *
 * A maze is the result of partitioning free space around a graph's nodes by @ref mkMaze.
 */

typedef struct {
  int ncells, ngcells;
  cell* cells;     ///< @brief cells not corresponding to graph nodes
  cell* gcells;    ///< @brief cells corresponding to graph nodes
  sgraph* sg;      ///< @brief search graph
  Dt_t* hchans;    ///< @brief set of horizontal @ref channel "channels", created by @ref extractHChans.
  Dt_t* vchans;    ///< @brief set of vertical @ref channel "channels", created by @ref extractVChans
} maze;

extern maze* mkMaze(graph_t*);
extern void freeMaze (maze*);
void updateWts (sgraph* g, cell* cp, sedge* ep);
#ifdef DEBUG
extern int odb_flags;
#define ODB_MAZE    1
#define ODB_SGRAPH  2
#define ODB_ROUTE   4
#define ODB_CHANG   8
#define ODB_IGRAPH 16
#endif
