/**
 * @file
 * @brief @ref snode priority queue for @ref shortPath in @ref sgraph
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

#include "../cgraph/alloc.h"
#include "sgraph.h"

#define N_VAL(n) (n)->n_val
#define N_IDX(n) (n)->n_idx
#define N_DAD(n) (n)->n_dad
#define N_EDGE(n) (n)->n_edge
#define E_WT(e) (e->weight)

void PQgen(int sz);
void PQfree(void);
void PQinit(void);
void PQcheck (void);
void PQupheap(int);
int PQ_insert(snode* np);
void PQdownheap (int k);
snode* PQremove (void);
void PQupdate (snode* n, int d);
void PQprint (void);
