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

/*-----------------------------------------------------------------------------
| Definitions and global variables.
-----------------------------------------------------------------------------*/
#include "rectangle.h"
#include "index.h"

#ifndef METHODS
#define METHODS 1
#endif /*METHODS*/
/* variables for finding a partition */
    struct PartitionVars {
    int partition[NODECARD + 1];
    int taken[NODECARD + 1];
    int count[2];
    struct Rect cover[2];
    int area[2];
};

typedef struct split_q_s {
    struct Branch BranchBuf[NODECARD + 1];
    struct Rect CoverSplit;
    unsigned int CoverSplitArea;
    struct PartitionVars Partitions[METHODS];
} SplitQ_t;

void SplitNode(RTree_t *, Node_t *, Branch_t *, Node_t **);

