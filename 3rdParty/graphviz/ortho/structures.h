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

#include "../common/types.h"
#include "../cgraph/cgraph.h"
#include "rawgraph.h"

typedef struct {
    double p1, p2;
} paird;

typedef struct {
    int a,b;
} pair_;

typedef struct {
    pair_ t1, t2;
} pair2;

typedef enum {B_NODE, B_UP, B_LEFT, B_DOWN, B_RIGHT} bend_;

/* Example : segment connecting maze point (3,2)
 * and (3,8) has isVert = 1, common coordinate = 3, p1 = 2, p2 = 8
 */
typedef struct segment {
  bool isVert;
  double comm_coord;  /* the common coordinate */
  paird p;      /* end points */
  bend_ l1, l2;
  int ind_no;      /* index number of this segment in its channel */
  int track_no;    /* track number assigned in the channel */
  struct segment* prev;
  struct segment* next;
} segment;

typedef struct {
  size_t n;
  segment* segs;
} route;

typedef struct {
  Dtlink_t link;
  paird p;   /* extrema of channel */
  int cnt;   /* number of segments */
  segment** seg_list; /* array of segment pointers */
  rawgraph* G;
  struct cell* cp;
} channel;

#define N_DAD(n) (n)->n_dad
