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

#include "index.h"

typedef struct Branch {
    Rect_t rect;
    struct Node *child;
} Branch_t;

typedef struct Node {
    int count;
    int level;            /* 0 is leaf, others positive */
    struct Branch branch[NODECARD];
} Node_t;

void InitNode(Node_t *);
void InitBranch(Branch_t *);
Rect_t NodeCover(Node_t *);
int PickBranch(Rect_t *, Node_t *);
int AddBranch(RTree_t *, Branch_t *, Node_t *, Node_t **);
void DisconBranch(Node_t *, int);
void PrintBranch(int, Branch_t *);
Node_t *RTreeNewNode(void);
#ifdef RTDEBUG
void PrintNode(Node_t * n);
void PrintBranch(int i, Branch_t * b);
#endif
