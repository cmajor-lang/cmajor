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
#include "../cgraph/alloc.h"
#include "sgraph.h"
#include "fPQ.h"

void
gsave (sgraph* G)
{
    int i;
    G->save_nnodes = G->nnodes;
    G->save_nedges = G->nedges;
    for (i = 0; i < G->nnodes; i++)
	G->nodes[i].save_n_adj =  G->nodes[i].n_adj;
}

void
reset(sgraph* G)
{
    int i;
    G->nnodes = G->save_nnodes;
    G->nedges = G->save_nedges;
    for (i = 0; i < G->nnodes; i++)
	G->nodes[i].n_adj = G->nodes[i].save_n_adj;
    for (; i < G->nnodes+2; i++)
	G->nodes[i].n_adj = 0;
}

void
initSEdges (sgraph* g, int maxdeg)
{
    int i;
    int* adj = (int*) gv_calloc(6 * g->nnodes + 2 * maxdeg, sizeof(int));
    g->edges = (sedge*) gv_calloc(3 * g->nnodes + maxdeg, sizeof(sedge));
    for (i = 0; i < g->nnodes; i++) {
	g->nodes[i].adj_edge_list = adj;
	adj += 6;
    }
    for (; i < g->nnodes+2; i++) {
	g->nodes[i].adj_edge_list = adj;
	adj += maxdeg;
    }
}

sgraph*
createSGraph (int nnodes)
{
    sgraph* g = (sgraph*) gv_alloc(sizeof(sgraph));

	/* create the nodes vector in the search graph */
    g->nnodes = 0;
    g->nodes = (snode*)gv_calloc(nnodes, sizeof(snode));
    return g;
}

snode*
createSNode (sgraph* g)
{
    snode* np = g->nodes+g->nnodes;
    np->index = g->nnodes;
    g->nnodes++;
    return np;
}

static void
addEdgeToNode (snode* np, int idx)
{
    np->adj_edge_list[np->n_adj] = idx;
    np->n_adj++;
}

sedge*
createSEdge (sgraph* g, snode* v1, snode* v2, double wt)
{
    sedge* e;
    int idx = g->nedges++;

    e = g->edges + idx;
    e->v1 = v1->index;
    e->v2 = v2->index;
    e->weight = wt;
    e->cnt = 0;

    addEdgeToNode (v1, idx);
    addEdgeToNode (v2, idx);

    return e;
}

void
freeSGraph (sgraph* g)
{
    free (g->nodes[0].adj_edge_list);
    free (g->nodes);
    free (g->edges);
    free (g);
}

#include "fPQ.h"

/* shortest path:
 * Constructs the path of least weight between from and to.
 *
 * Assumes graph, node and edge type, and that nodes
 * have associated values N_VAL, N_IDX, and N_DAD, the first two
 * being ints, the last being a node*. Edges have a E_WT function
 * to specify the edge length or weight.
 *
 * Assumes there are functions:
 *  agnnodes: graph -> int           number of nodes in the graph
 *  agfstnode, agnxtnode : iterators over the nodes in the graph
 *  agfstedge, agnxtedge : iterators over the edges attached to a node
 *  adjacentNode : given an edge e and an endpoint n of e, returns the
 *                 other endpoint.
 *
 * The path is given by
 *  to, N_DAD(to), N_DAD(N_DAD(to)), ..., from
 */

#define UNSEEN INT_MIN

static snode*
adjacentNode(sgraph* g, sedge* e, snode* n)
{
    if (e->v1==n->index)
	return &g->nodes[e->v2];
    else
	return &g->nodes[e->v1];
}

int
shortPath (sgraph* g, snode* from, snode* to)
{
    snode* n;
    sedge* e;
    snode* adjn;
    int d;
    int   x, y;

    for (x = 0; x<g->nnodes; x++) {
	snode* temp = &g->nodes[x];
	N_VAL(temp) = UNSEEN;
    }

    PQinit();
    if (PQ_insert (from)) return 1;
    N_DAD(from) = NULL;
    N_VAL(from) = 0;

    while ((n = PQremove())) {
#ifdef DEBUG
	fprintf (stderr, "process %d\n", n->index);
#endif
	N_VAL(n) *= -1;
	if (n == to) break;
	for (y=0; y<n->n_adj; y++) {
	    e = &g->edges[n->adj_edge_list[y]];
	    adjn = adjacentNode(g, e, n);
	    if (N_VAL(adjn) < 0) {
		d = -(N_VAL(n) + E_WT(e));
		if (N_VAL(adjn) == UNSEEN) {
#ifdef DEBUG
		    fprintf (stderr, "new %d (%d)\n", adjn->index, -d);
#endif
		    N_VAL(adjn) = d;
		    if (PQ_insert(adjn)) return 1;
		    N_DAD(adjn) = n;
		    N_EDGE(adjn) = e;
            	}
		else {
		    if (N_VAL(adjn) < d) {
#ifdef DEBUG
			fprintf (stderr, "adjust %d (%d)\n", adjn->index, -d);
#endif
			PQupdate(adjn, d);
			N_DAD(adjn) = n;
			N_EDGE(adjn) = e;
		    }
		}
	    }
	}
    }

    return 0;
}

