/**
 * @file
 * @brief trapezoid elements and utilities for partition.c
 *
 * See [Fast polygon triangulation based on Seidel's algorithm](http://gamma.cs.unc.edu/SEIDEL/)
 *
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

/* Segment attributes */

typedef struct {
  pointf v0, v1;       /* two endpoints */
  bool is_inserted;      /* inserted in trapezoidation yet ? */
  int root0, root1;     /* root nodes in Q */
  int next;         /* Next logical segment */
  int prev;         /* Previous segment */
} segment_t;


/* Trapezoid attributes */

typedef struct {
  int lseg, rseg;       /* two adjoining segments */
  pointf hi, lo;       /* max/min y-values */
  int u0, u1;
  int d0, d1;
  int sink;         /* pointer to corresponding in Q */
  int usave, uside;     /* I forgot what this means */
  int state;
} trap_t;

/// an array of trapezoids
typedef struct {
  size_t length;
  trap_t *data;
} traps_t;

#define ST_VALID 1      /* for trapezium state */
#define ST_INVALID 2

#define C_EPS 1.0e-7        /* tolerance value: Used for making */
                /* all decisions about collinearity or */
                /* left/right of segment. Decrease */
                /* this value if the input points are */
                /* spaced very close together */
#define FP_EQUAL(s, t) (fabs(s - t) <= C_EPS)

/**
 * @brief double floating point three-way comparison
 *
 * Returns -1, 0, or 1 if f1 respectively, less than,
 * almost equal with tolerance C_EPS, or greater than f2.
 * The purpose of the function is workaround precision issues.
 */

static inline int dfp_cmp(double f1, double f2) {
  double d = f1 - f2;
  if (d < -C_EPS)
    return -1;
  if (d > C_EPS)
    return 1;
  return 0;
}

#define _equal_to(v0,v1) \
  (FP_EQUAL((v0)->y, (v1)->y) && FP_EQUAL((v0)->x, (v1)->x))

#define _greater_than(v0, v1) \
  (((v0)->y > (v1)->y + C_EPS) ? true : (((v0)->y < (v1)->y - C_EPS) ? false : ((v0)->x > (v1)->x)))

extern traps_t construct_trapezoids(int, segment_t*, int*);
