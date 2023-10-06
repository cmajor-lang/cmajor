/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/


/* 
 * Decompose finds the connected components of a graph.
 * It searches the temporary edges and ignores non-root nodes.
 * The roots of the search are the real nodes of the graph,
 * but any virtual nodes discovered are also included in the
 * component.
 */

#include "../cgraph/stack.h"
#include "dot.h"

static node_t *Last_node;
static size_t Cmark;

static void 
begin_component(graph_t* g)
{
    Last_node = GD_nlist(g) = NULL;
}

static void 
add_to_component(graph_t* g, node_t * n)
{
    GD_n_nodes(g)++;
    ND_mark(n) = Cmark;
    if (Last_node) {
	ND_prev(n) = Last_node;
	ND_next(Last_node) = n;
    } else {
	ND_prev(n) = NULL;
	GD_nlist(g) = n;
    }
    Last_node = n;
    ND_next(n) = NULL;
}

static void 
end_component(graph_t* g)
{
    int i;

    i = GD_comp(g).size++;
    GD_comp(g).list = ALLOC(GD_comp(g).size, GD_comp(g).list, node_t *);
    GD_comp(g).list[i] = GD_nlist(g);
}

static void push(gv_stack_t *sp, node_t *np) {
  ND_mark(np) = Cmark + 1;
  stack_push_or_exit(sp, np);
}

static node_t *pop(gv_stack_t *sp) {

  if (stack_is_empty(sp)) {
    return NULL;
  }

  return (node_t*) stack_pop(sp);
}

/* search_component:
 * iterative dfs for components.
 * We process the edges in reverse order of the recursive version to maintain
 * the processing order of the nodes.
 * Since are using a stack, we need to indicate nodes on the stack. Nodes unprocessed
 * in this call to decompose will have mark < Cmark; processed nodes will have mark=Cmark;
 * so we use mark = Cmark+1 to indicate nodes on the stack.
 */
static void search_component(gv_stack_t *stk, graph_t *g, node_t *n) {
    int c, i;
    elist vec[4];
    node_t *other;
    edge_t *e;
    edge_t **ep;

    push(stk, n);
    while ((n = pop(stk))) {
	if (ND_mark(n) == Cmark) continue;
	add_to_component(g, n);
	vec[0] = ND_out(n);
	vec[1] = ND_in(n);
	vec[2] = ND_flat_out(n);
	vec[3] = ND_flat_in(n);

	for (c = 3; c >= 0; c--) {
	    if (vec[c].list) {
		for (i = vec[c].size-1, ep = vec[c].list+i; i >= 0; i--, ep--) {
		    e = *ep;
		    if ((other = aghead(e)) == n)
			other = agtail(e);
		    if ((ND_mark(other) != Cmark) && (other == UF_find(other)))
			push(stk, other);
		}
	    }
	}
    }
}

void decompose(graph_t * g, int pass)
{
    graph_t *subg;
    node_t *n, *v;
    gv_stack_t stk = {0};

    if (++Cmark == 0)
	Cmark = 1;
    GD_n_nodes(g) = GD_comp(g).size = 0;
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	v = n;
	if ((pass > 0) && (subg = ND_clust(v)))
	    v = GD_rankleader(subg)[ND_rank(v)];
	else if (v != UF_find(v))
	    continue;
	if (ND_mark(v) != Cmark) {
	    begin_component(g);
	    search_component(&stk, g, v);
	    end_component(g);
	}
    }
    stack_reset(&stk);
}
