/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "../gvc/config.h"
#include "../common/boxes.h"
#include "../cgraph/alloc.h"
#include "../cgraph/bitarray.h"
#include "../cgraph/prisize_t.h"
#include "partition.h"
#include "trap.h"

#undef DEBUG

#define NPOINTS 4   /* only rectangles */

#define TR_FROM_UP 1        /* for traverse-direction */
#define TR_FROM_DN 2

#define SP_SIMPLE_LRUP 1    /* for splitting trapezoids */
#define SP_SIMPLE_LRDN 2
#define SP_2UP_2DN     3
#define SP_2UP_LEFT    4
#define SP_2UP_RIGHT   5
#define SP_2DN_LEFT    6
#define SP_2DN_RIGHT   7
#define SP_NOSPLIT    -1

#define DOT(v0, v1) ((v0).x * (v1).x + (v0).y * (v1).y)
#define CROSS_SINE(v0, v1) ((v0).x * (v1).y - (v1).x * (v0).y)
#define LENGTH(v0) hypot((v0).x, (v0).y)

#ifndef HAVE_SRAND48
#define srand48 srand
#endif
#ifdef _WIN32
extern double drand48(void);
#endif

typedef struct {
  int vnum;
  int next;         /* Circularly linked list  */
  int prev;         /* describing the monotone */
  int marked;           /* polygon */
} monchain_t;

typedef struct {
  pointf pt;
  int vnext[4];         /* next vertices for the 4 chains */
  int vpos[4];          /* position of v in the 4 chains */
  int nextfree;
} vertexchain_t;

static int chain_idx, mon_idx;
	/* Table to hold all the monotone */
	/* polygons . Each monotone polygon */
	/* is a circularly linked list */
static monchain_t* mchain;
	/* chain init. information. This */
	/* is used to decide which */
	/* monotone polygon to split if */
	/* there are several other */
	/* polygons touching at the same */
	/* vertex  */
static vertexchain_t* vert;
	/* contains position of any vertex in */
	/* the monotone chain for the polygon */
static int* mon;

/* return a new mon structure from the table */
#define newmon() (++mon_idx)
/* return a new chain element from the table */
#define new_chain_element() (++chain_idx)

static void
convert (boxf bb, int flip, int ccw, pointf* pts)
{
    pts[0] = bb.LL;
    pts[2] = bb.UR;
    if (ccw) {
	pts[1].x = bb.UR.x;
	pts[1].y = bb.LL.y;
	pts[3].x = bb.LL.x;
	pts[3].y = bb.UR.y;
    }
    else {
	pts[1].x = bb.LL.x;
	pts[1].y = bb.UR.y;
	pts[3].x = bb.UR.x;
	pts[3].y = bb.LL.y;
    }
    if (flip) {
	int i;
	for (i = 0; i < NPOINTS; i++) {
	    double tmp = pts[i].y;
	    pts[i].y = pts[i].x;
	    pts[i].x = -tmp;
	}
    }
}

static int
store (segment_t* seg, int first, pointf* pts)
{
    int i, last = first + NPOINTS - 1;
    int j = 0;

    for (i = first; i <= last; i++, j++) {
	if (i == first) {
	    seg[i].next = first+1;
	    seg[i].prev = last;
	}
	else if (i == last) {
	    seg[i].next = first;
	    seg[i].prev = last-1;
	}
	else {
	    seg[i].next = i+1;
	    seg[i].prev = i-1;
	}
	seg[i].is_inserted = false;
	seg[seg[i].prev].v1 = seg[i].v0 = pts[j];
    }
    return (last+1);
}

static void
genSegments (cell* cells, int ncells, boxf bb, segment_t* seg, int flip)
{
    int j = 0, i = 1;
    pointf pts[4];

    convert (bb, flip, 1, pts);
    i = store (seg, i, pts);
    for (j = 0; j < ncells; j++) {
	convert (cells[j].bb, flip, 0, pts);
	i = store (seg, i, pts);
    }
}

/* Generate a random permutation of the segments 1..n */
static void
generateRandomOrdering(int n, int* permute)
{
    int i, j, tmp;
    for (i = 0; i <= n; i++) permute[i] = i;

    for (i = 1; i <= n; i++) {
	j = i + drand48() * (n + 1 - i);
	if (j != i) {
	    tmp = permute[i];
	    permute [i] = permute[j];
            permute [j] = tmp;
	}
    }
}

/* Function returns true if the trapezoid lies inside the polygon */
static bool
inside_polygon (trap_t *t, segment_t* seg)
{
  int rseg = t->rseg;

  if (t->state == ST_INVALID)
    return false;

  if (t->lseg <= 0 || t->rseg <= 0)
    return false;

  if ((t->u0 <= 0 && t->u1 <= 0) || (t->d0 <= 0 && t->d1 <= 0)) /* triangle */
    return _greater_than(&seg[rseg].v1, &seg[rseg].v0);

  return false;
}

static double
get_angle (pointf *vp0, pointf *vpnext, pointf *vp1)
{
  pointf v0, v1;

  v0.x = vpnext->x - vp0->x;
  v0.y = vpnext->y - vp0->y;

  v1.x = vp1->x - vp0->x;
  v1.y = vp1->y - vp0->y;

  if (CROSS_SINE(v0, v1) >= 0)	/* sine is positive */
    return DOT(v0, v1)/LENGTH(v0)/LENGTH(v1);
  else
    return -1.0 * DOT(v0, v1)/LENGTH(v0)/LENGTH(v1) - 2;
}

/* (v0, v1) is the new diagonal to be added to the polygon. Find which */
/* chain to use and return the positions of v0 and v1 in p and q */
static void
get_vertex_positions (int v0, int v1, int *ip, int *iq)
{
  vertexchain_t *vp0, *vp1;
  int i;
  double angle, temp;
  int tp = 0, tq = 0;

  vp0 = &vert[v0];
  vp1 = &vert[v1];

  /* p is identified as follows. Scan from (v0, v1) rightwards till */
  /* you hit the first segment starting from v0. That chain is the */
  /* chain of our interest */

  angle = -4.0;
  for (i = 0; i < 4; i++)
    {
      if (vp0->vnext[i] <= 0)
	continue;
      if ((temp = get_angle(&vp0->pt, &(vert[vp0->vnext[i]].pt),
			    &vp1->pt)) > angle)
	{
	  angle = temp;
	  tp = i;
	}
    }

  *ip = tp;

  /* Do similar actions for q */

  angle = -4.0;
  for (i = 0; i < 4; i++)
    {
      if (vp1->vnext[i] <= 0)
	continue;
      if ((temp = get_angle(&vp1->pt, &(vert[vp1->vnext[i]].pt),
			    &vp0->pt)) > angle)
	{
	  angle = temp;
	  tq = i;
	}
    }

  *iq = tq;
}

/* v0 and v1 are specified in anti-clockwise order with respect to
 * the current monotone polygon mcur. Split the current polygon into
 * two polygons using the diagonal (v0, v1)
 */
static int
make_new_monotone_poly (int mcur, int v0, int v1)
{
  int p, q, ip, iq;
  int mnew = newmon();
  int i, j, nf0, nf1;
  vertexchain_t *vp0, *vp1;

  vp0 = &vert[v0];
  vp1 = &vert[v1];

  get_vertex_positions(v0, v1, &ip, &iq);

  p = vp0->vpos[ip];
  q = vp1->vpos[iq];

  /* At this stage, we have got the positions of v0 and v1 in the */
  /* desired chain. Now modify the linked lists */

  i = new_chain_element();	/* for the new list */
  j = new_chain_element();

  mchain[i].vnum = v0;
  mchain[j].vnum = v1;

  mchain[i].next = mchain[p].next;
  mchain[mchain[p].next].prev = i;
  mchain[i].prev = j;
  mchain[j].next = i;
  mchain[j].prev = mchain[q].prev;
  mchain[mchain[q].prev].next = j;

  mchain[p].next = q;
  mchain[q].prev = p;

  nf0 = vp0->nextfree;
  nf1 = vp1->nextfree;

  vp0->vnext[ip] = v1;

  vp0->vpos[nf0] = i;
  vp0->vnext[nf0] = mchain[mchain[i].next].vnum;
  vp1->vpos[nf1] = j;
  vp1->vnext[nf1] = v0;

  vp0->nextfree++;
  vp1->nextfree++;

#if DEBUG > 0
  fprintf(stderr, "make_poly: mcur = %d, (v0, v1) = (%d, %d)\n", mcur, v0, v1);
  fprintf(stderr, "next posns = (p, q) = (%d, %d)\n", p, q);
#endif

  mon[mcur] = p;
  mon[mnew] = i;
  return mnew;
}

/* recursively visit all the trapezoids */
static void traverse_polygon(bitarray_t *visited, boxes_t *decomp,
                             segment_t *seg, traps_t *tr, int mcur, int trnum,
                             int from, int flip, int dir) {
  trap_t *t;
  int mnew;
  int v0, v1;

  if (trnum <= 0 || bitarray_get(*visited, (size_t)trnum))
    return;

  t = &tr->data[trnum];

  bitarray_set(visited, (size_t)trnum, true);

  if (t->hi.y > t->lo.y + C_EPS && FP_EQUAL(seg[t->lseg].v0.x, seg[t->lseg].v1.x) &&
      FP_EQUAL(seg[t->rseg].v0.x, seg[t->rseg].v1.x)) {
      boxf newbox = {0};
      if (flip) {
          newbox.LL.x = t->lo.y;
          newbox.LL.y = -seg[t->rseg].v0.x;
          newbox.UR.x = t->hi.y;
          newbox.UR.y = -seg[t->lseg].v0.x;
      } else {
          newbox.LL.x = seg[t->lseg].v0.x;
          newbox.LL.y = t->lo.y;
          newbox.UR.x = seg[t->rseg].v0.x;
          newbox.UR.y = t->hi.y;
      }
      boxes_append(decomp, newbox);
  }

  /* We have much more information available here. */
  /* rseg: goes upwards   */
  /* lseg: goes downwards */

  /* Initially assume that dir = TR_FROM_DN (from the left) */
  /* Switch v0 and v1 if necessary afterwards */


  /* special cases for triangles with cusps at the opposite ends. */
  /* take care of this first */
  if (t->u0 <= 0 && t->u1 <= 0)
    {
      if (t->d0 > 0 && t->d1 > 0) /* downward opening triangle */
	{
	  v0 = tr->data[t->d1].lseg;
	  v1 = t->lseg;
	  if (from == t->d1)
	    {
	      mnew = make_new_monotone_poly(mcur, v1, v0);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
	      traverse_polygon(visited, decomp, seg, tr, mnew, t->d0, trnum, flip, TR_FROM_UP);
	    }
	  else
	    {
	      mnew = make_new_monotone_poly(mcur, v0, v1);
	      traverse_polygon (visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
	      traverse_polygon (visited, decomp, seg, tr, mnew, t->d1, trnum, flip, TR_FROM_UP);
	    }
	}
      else
	{
	  /* Just traverse all neighbours */
	  traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
	  traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
	  traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
	  traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
	}
    }

  else if (t->d0 <= 0 && t->d1 <= 0)
    {
      if (t->u0 > 0 && t->u1 > 0) /* upward opening triangle */
	{
	  v0 = t->rseg;
	  v1 = tr->data[t->u0].rseg;
	  if (from == t->u1)
	    {
	      mnew = make_new_monotone_poly(mcur, v1, v0);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
	      traverse_polygon(visited, decomp, seg, tr, mnew, t->u0, trnum, flip, TR_FROM_DN);
	    }
	  else
	    {
	      mnew = make_new_monotone_poly(mcur, v0, v1);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
	      traverse_polygon(visited, decomp, seg, tr, mnew, t->u1, trnum, flip, TR_FROM_DN);
	    }
	}
      else
	{
	  /* Just traverse all neighbours */
	  traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
	  traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
	  traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
	  traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
	}
    }

  else if (t->u0 > 0 && t->u1 > 0)
    {
      if (t->d0 > 0 && t->d1 > 0) /* downward + upward cusps */
	{
	  v0 = tr->data[t->d1].lseg;
	  v1 = tr->data[t->u0].rseg;
	  if ((dir == TR_FROM_DN && t->d1 == from) ||
	      (dir == TR_FROM_UP && t->u1 == from))
	    {
	      mnew = make_new_monotone_poly(mcur, v1, v0);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
	      traverse_polygon(visited, decomp, seg, tr, mnew, t->u0, trnum, flip, TR_FROM_DN);
	      traverse_polygon(visited, decomp, seg, tr, mnew, t->d0, trnum, flip, TR_FROM_UP);
	    }
	  else
	    {
	      mnew = make_new_monotone_poly(mcur, v0, v1);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
	      traverse_polygon(visited, decomp, seg, tr, mnew, t->u1, trnum, flip, TR_FROM_DN);
	      traverse_polygon(visited, decomp, seg, tr, mnew, t->d1, trnum, flip, TR_FROM_UP);
	    }
	}
      else			/* only downward cusp */
	{
	  if (_equal_to(&t->lo, &seg[t->lseg].v1))
	    {
	      v0 = tr->data[t->u0].rseg;
	      v1 = seg[t->lseg].next;

	      if (dir == TR_FROM_UP && t->u0 == from)
		{
		  mnew = make_new_monotone_poly(mcur, v1, v0);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d0, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u1, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d1, trnum, flip, TR_FROM_UP);
		}
	      else
		{
		  mnew = make_new_monotone_poly(mcur, v0, v1);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u0, trnum, flip, TR_FROM_DN);
		}
	    }
	  else
	    {
	      v0 = t->rseg;
	      v1 = tr->data[t->u0].rseg;
	      if (dir == TR_FROM_UP && t->u1 == from)
		{
		  mnew = make_new_monotone_poly(mcur, v1, v0);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d1, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d0, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u0, trnum, flip, TR_FROM_DN);
		}
	      else
		{
		  mnew = make_new_monotone_poly(mcur, v0, v1);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u1, trnum, flip, TR_FROM_DN);
		}
	    }
	}
    }
  else if (t->u0 > 0 || t->u1 > 0) /* no downward cusp */
    {
      if (t->d0 > 0 && t->d1 > 0) /* only upward cusp */
	{
	  if (_equal_to(&t->hi, &seg[t->lseg].v0))
	    {
	      v0 = tr->data[t->d1].lseg;
	      v1 = t->lseg;
	      if (!(dir == TR_FROM_DN && t->d0 == from))
		{
		  mnew = make_new_monotone_poly(mcur, v1, v0);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d0, trnum, flip, TR_FROM_UP);
		}
	      else
		{
		  mnew = make_new_monotone_poly(mcur, v0, v1);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u1, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d1, trnum, flip, TR_FROM_UP);
		}
	    }
	  else
	    {
	      v0 = tr->data[t->d1].lseg;
	      v1 = seg[t->rseg].next;

	      if (dir == TR_FROM_DN && t->d1 == from)
		{
		  mnew = make_new_monotone_poly(mcur, v1, v0);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u1, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d0, trnum, flip, TR_FROM_UP);
		}
	      else
		{
		  mnew = make_new_monotone_poly(mcur, v0, v1);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d1, trnum, flip, TR_FROM_UP);
		}
	    }
	}
      else			/* no cusp */
	{
	  if (_equal_to(&t->hi, &seg[t->lseg].v0) &&
	      _equal_to(&t->lo, &seg[t->rseg].v0))
	    {
	      v0 = t->rseg;
	      v1 = t->lseg;
	      if (dir == TR_FROM_UP)
		{
		  mnew = make_new_monotone_poly(mcur, v1, v0);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d1, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d0, trnum, flip, TR_FROM_UP);
		}
	      else
		{
		  mnew = make_new_monotone_poly(mcur, v0, v1);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u1, trnum, flip, TR_FROM_DN);
		}
	    }
	  else if (_equal_to(&t->hi, &seg[t->rseg].v1) &&
		   _equal_to(&t->lo, &seg[t->lseg].v1))
	    {
	      v0 = seg[t->rseg].next;
	      v1 = seg[t->lseg].next;

	      if (dir == TR_FROM_UP)
		{
		  mnew = make_new_monotone_poly(mcur, v1, v0);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d1, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->d0, trnum, flip, TR_FROM_UP);
		}
	      else
		{
		  mnew = make_new_monotone_poly(mcur, v0, v1);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u0, trnum, flip, TR_FROM_DN);
		  traverse_polygon(visited, decomp, seg, tr, mnew, t->u1, trnum, flip, TR_FROM_DN);
		}
	    }
	  else			/* no split possible */
	    {
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->u0, trnum, flip, TR_FROM_DN);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->d0, trnum, flip, TR_FROM_UP);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->u1, trnum, flip, TR_FROM_DN);
	      traverse_polygon(visited, decomp, seg, tr, mcur, t->d1, trnum, flip, TR_FROM_UP);
	    }
	}
    }
}

static void
monotonate_trapezoids(int nsegs, segment_t *seg, traps_t *tr,
    int flip, boxes_t *decomp) {
    int i;
    int tr_start;
    bitarray_t visited = bitarray_new(tr->length);

    mchain = (monchain_t*) gv_calloc(tr->length, sizeof(monchain_t));
    vert = (vertexchain_t*) gv_calloc(nsegs + 1, sizeof(vertexchain_t));
    mon = (int*) gv_calloc(nsegs, sizeof(int));

  /* First locate a trapezoid which lies inside the polygon */
  /* and which is triangular */
    for (i = 0; i < tr->length; i++)
	if (inside_polygon(&tr->data[i], seg)) break;
    tr_start = i;

  /* Initialise the mon data-structure and start spanning all the */
  /* trapezoids within the polygon */

    for (i = 1; i <= nsegs; i++) {
	mchain[i].prev = seg[i].prev;
	mchain[i].next = seg[i].next;
	mchain[i].vnum = i;
	vert[i].pt = seg[i].v0;
	vert[i].vnext[0] = seg[i].next; /* next vertex */
	vert[i].vpos[0] = i;	/* locn. of next vertex */
	vert[i].nextfree = 1;
    }

    chain_idx = nsegs;
    mon_idx = 0;
    mon[0] = 1;			/* position of any vertex in the first */
				/* chain  */

  /* traverse the polygon */
    if (tr->data[tr_start].u0 > 0)
	traverse_polygon(&visited, decomp, seg, tr, 0, tr_start,
	                 tr->data[tr_start].u0, flip, TR_FROM_UP);
    else if (tr->data[tr_start].d0 > 0)
	traverse_polygon(&visited, decomp, seg, tr, 0, tr_start,
	                 tr->data[tr_start].d0, flip, TR_FROM_DN);

    bitarray_reset(&visited);
    free (mchain);
    free (vert);
    free (mon);
}

static bool
rectIntersect (boxf *d, const boxf *r0, const boxf *r1)
{
    double t = fmax(r0->LL.x, r1->LL.x);
    d->UR.x = fmin(r0->UR.x, r1->UR.x);
    d->LL.x = t;

    t = fmax(r0->LL.y, r1->LL.y);
    d->UR.y = fmin(r0->UR.y, r1->UR.y);
    d->LL.y = t;

    return !(d->LL.x >= d->UR.x || d->LL.y >= d->UR.y);
}

#if DEBUG > 1
static void
dumpTrap (trap_t* tr, int n)
{
    int i;
    for (i = 1; i <= n; i++) {
      tr++;
      fprintf (stderr, "%d : %d %d (%f,%f) (%f,%f) %d %d %d %d\n", i,
         tr->lseg, tr->rseg, tr->hi.x, tr->hi.y, tr->lo.x, tr->lo.y,
         tr->u0, tr->u1,  tr->d0, tr->d1);
      fprintf (stderr, "    %d %d %d %d\n", tr->sink, tr->usave,
         tr->uside, tr->state);
    }
    fprintf (stderr, "====\n");
}

static void
dumpSegs (segment_t* sg, int n)
{
    int i;
    for (i = 1; i <= n; i++) {
      sg++;
      fprintf (stderr, "%d : (%f,%f) (%f,%f) %d %d %d %d %d\n", i,
         sg->v0.x, sg->v0.y, sg->v1.x, sg->v1.y,
         (int)sg->is_inserted, sg->root0,  sg->root1, sg->next, sg->prev);
    }
    fprintf (stderr, "====\n");
}
#endif

boxf *partition(cell *cells, int ncells, size_t *nrects, boxf bb) {
    int nsegs = 4*(ncells+1);
    segment_t* segs = (segment_t*) gv_calloc(nsegs + 1, sizeof(segment_t));
    int* permute = (int*) gv_calloc(nsegs + 1, sizeof(int));

	static constexpr bool DEBUG = false;

    if (DEBUG) {
	fprintf (stderr, "cells = %d segs = %d traps = dynamic\n", ncells, nsegs);
    }
    genSegments (cells, ncells, bb, segs, 0);
    if (DEBUG) {
	fprintf (stderr, "%d\n\n", ncells+1);
	for (int i = 1; i <= nsegs; i++) {
	    if (i%4 == 1) fprintf(stderr, "4\n");
	    fprintf (stderr, "%f %f\n", segs[i].v0.x, segs[i].v0.y);
	    if (i%4 == 0) fprintf(stderr, "\n");
	}
    }
    srand48(173);
    generateRandomOrdering (nsegs, permute);
    traps_t hor_traps = construct_trapezoids(nsegs, segs, permute);
    if (DEBUG) {
	fprintf (stderr, "hor traps = %" PRISIZE_T "\n", hor_traps.length);
    }
    boxes_t hor_decomp = {0};
    monotonate_trapezoids(nsegs, segs, &hor_traps, 0, &hor_decomp);
    free(hor_traps.data);

    genSegments (cells, ncells, bb, segs, 1);
    generateRandomOrdering (nsegs, permute);
    traps_t ver_traps = construct_trapezoids(nsegs, segs, permute);
    if (DEBUG) {
	fprintf (stderr, "ver traps = %" PRISIZE_T "\n", ver_traps.length);
    }
    boxes_t vert_decomp = {0};
    monotonate_trapezoids(nsegs, segs, &ver_traps, 1, &vert_decomp);
    free(ver_traps.data);

    boxes_t rs = {0};
    for (size_t i = 0; i < vert_decomp.size; ++i)
	for (size_t j = 0; j < hor_decomp.size; ++j) {
	    boxf newbox = {0};
	    if (rectIntersect(&newbox, &vert_decomp.data[i], &hor_decomp.data[j]))
		boxes_append(&rs, newbox);
	}

    free (segs);
    free (permute);
    boxes_free(&hor_decomp);
    boxes_free(&vert_decomp);
    *nrects = rs.size;
    return rs.data;
}
