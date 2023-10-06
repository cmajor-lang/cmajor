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

/*
 * this library is derived from an archived home directory of Antonin Guttman
 * that implemented the ideas described in
 * "R-trees: a dynamic index structure for spatial searching"
 * Antonin Guttman, University of California, Berkeley
 * SIGMOD '84 Proceedings of the 1984 ACM SIGMOD international conference on Management of data
 * ISBN:0-89791-128-8
 * http://dx.doi.org/10.1145/602259.602266
 * this copy of the code was retrieved from
 * http://web.archive.org/web/20030210112132/http://www.es.ucsc.edu/~tonig/rtrees/
 * we are using the quadratic node splitter only
 * we made a few cosmetic changes to fit our needs
 * per Antonin there is no copyright
 */

/*-----------------------------------------------------------------------------
| Global definitions.
-----------------------------------------------------------------------------*/

#ifndef NUMDIMS
#define NUMDIMS 2
#endif /*NUMDIMS*/
/* #define NDEBUG */
#define NUMSIDES 2*NUMDIMS
/* branching factor of a node */
/* #define NODECARD (int)((PGSIZE-(2*sizeof(int)))/sizeof(struct Branch))*/
#define NODECARD 64
typedef struct RTree RTree_t;

#include "rectangle.h"
#include "node.h"
#include "split.q.h"

#define CX(i)  (i)
#define NX(i)  (i+NUMDIMS)
#define CY(i)  (i+1)
#define NY(i)  (i+1+NUMDIMS)

typedef struct Leaf {
    Rect_t rect;
    void *data;
} Leaf_t;

typedef struct LeafList {
    struct LeafList *next;
    Leaf_t *leaf;
} LeafList_t;

    struct RTree {
    Node_t *root;

    SplitQ_t split;

    /* balance criterion for node splitting */
    int MinFill;
};

RTree_t *RTreeOpen(void);
int RTreeClose(RTree_t * rtp);
Node_t *RTreeNewIndex(void);
LeafList_t *RTreeSearch(RTree_t *, Node_t *, Rect_t *);
int RTreeInsert(RTree_t *, Rect_t *, void *, Node_t **, int);

LeafList_t *RTreeNewLeafList(Leaf_t * lp);
LeafList_t *RTreeLeafListAdd(LeafList_t * llp, Leaf_t * lp);
void RTreeLeafListFree(LeafList_t * llp);

#ifdef RTDEBUG
void PrintNode(Node_t *);
#endif
