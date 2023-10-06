/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/


/*  vladimir@cs.ualberta.ca,  9-Dec-1997
 *	merge edges with specified samehead/sametail onto the same port
 */

#include "dot.h"

#define MAXSAME 5		/* max no of same{head,tail} groups on a node */

typedef struct same_t {
    char *id;			/* group id */
    elist l;			/* edges in the group */
} same_t;

static int sameedge(same_t * same, int n_same, node_t * n, edge_t * e, char *id);
static void sameport(node_t * u, elist * l);

void dot_sameports(graph_t * g)
/* merge edge ports in G */
{
    node_t *n;
    edge_t *e;
    char *id;
    same_t samehead[MAXSAME];
    same_t sametail[MAXSAME];
    int n_samehead;		/* number of same_t groups on current node */
    int n_sametail;		/* number of same_t groups on current node */
    int i;

    E_samehead = agattr(g, AGEDGE, "samehead", NULL);
    E_sametail = agattr(g, AGEDGE, "sametail", NULL);
    if (!(E_samehead || E_sametail))
	return;
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	n_samehead = n_sametail = 0;
	for (e = agfstedge(g, n); e; e = agnxtedge(g, e, n)) {
	    if (aghead(e) == agtail(e)) continue;  /* Don't support same* for loops */
	    if (aghead(e) == n && E_samehead &&
	        (id = agxget(e, E_samehead))[0])
		n_samehead = sameedge(samehead, n_samehead, n, e, id);
	    else if (agtail(e) == n && E_sametail &&
	        (id = agxget(e, E_sametail))[0])
		n_sametail = sameedge(sametail, n_sametail, n, e, id);
	}
	for (i = 0; i < n_samehead; i++) {
	    if (samehead[i].l.size > 1)
		sameport(n, &samehead[i].l);
	    free_list(samehead[i].l);
	    /* I sure hope I don't need to free the char* id */
	}
	for (i = 0; i < n_sametail; i++) {
	    if (sametail[i].l.size > 1)
		sameport(n, &sametail[i].l);
	    free_list(sametail[i].l);
	    /* I sure hope I don't need to free the char* id */
	}
    }
}

static int sameedge(same_t * same, int n_same, node_t * n, edge_t * e, char *id)
/* register E in the SAME structure of N under ID. Uses static int N_SAME */
{
    int i;

    for (i = 0; i < n_same; i++)
	if (streq(same[i].id, id)) {
	    elist_append(e, same[i].l);
	    return n_same;
	}
    if (++n_same > MAXSAME) {
	n_same--;
	agerr(AGERR, "too many (> %d) same{head,tail} groups for node %s\n",
	      MAXSAME, agnameof(n));
	return n_same;
    }
    alloc_elist(1, same[i].l);
    elist_fastapp(e, same[i].l);
    same[i].id = id;
    return n_same;
}

static void sameport(node_t * u, elist * l)
/* make all edges in L share the same port on U. The port is placed on the
   node boundary and the average angle between the edges. FIXME: this assumes
   naively that the edges are straight lines, which is wrong if they are long.
   In that case something like concentration could be done.

   An arr_port is also computed that's ARR_LEN away from the node boundary.
   It's used for edges that don't themselves have an arrow.
*/
{
    node_t *v;
    edge_t *e, *f;
    int i;
    double x = 0, y = 0, x1, y1, x2, y2, r;
    port prt;

    /* Compute the direction vector (x,y) of the average direction. We compute
       with direction vectors instead of angles because else we have to first
       bring the angles within PI of each other. av(a,b)!=av(a,b+2*PI) */
    for (i = 0; i < l->size; i++) {
	e = l->list[i];
	if (aghead(e) == u)
	    v = agtail(e);
	else
	    v = aghead(e);
	x1 = ND_coord(v).x - ND_coord(u).x;
	y1 = ND_coord(v).y - ND_coord(u).y;
	r = hypot(x1, y1);
	x += x1 / r;
	y += y1 / r;
    }
    r = hypot(x, y);
    x /= r;
    y /= r;

    /* (x1,y1),(x2,y2) is a segment that must cross the node boundary */
    x1 = ND_coord(u).x;
    y1 = ND_coord(u).y;	/* center of node */
    r = MAX(ND_lw(u) + ND_rw(u), ND_ht(u) + GD_ranksep(agraphof(u)));	/* far away */
    x2 = x * r + ND_coord(u).x;
    y2 = y * r + ND_coord(u).y;
    {				/* now move (x1,y1) to the node boundary */
	pointf curve[4];		/* bezier control points for a straight line */
	curve[0].x = x1;
	curve[0].y = y1;
	curve[1].x = (2 * x1 + x2) / 3;
	curve[1].y = (2 * y1 + y2) / 3;
	curve[2].x = (2 * x2 + x1) / 3;
	curve[2].y = (2 * y2 + y1) / 3;
	curve[3].x = x2;
	curve[3].y = y2;

	shape_clip(u, curve);
	x1 = curve[0].x - ND_coord(u).x;
	y1 = curve[0].y - ND_coord(u).y;
    }

    /* compute PORT on the boundary */
    prt.p.x = ROUND(x1);
    prt.p.y = ROUND(y1);
    prt.bp = 0;
    prt.order =
	(MC_SCALE * (ND_lw(u) + prt.p.x)) / (ND_lw(u) + ND_rw(u));
    prt.constrained = false;
    prt.defined = true;
    prt.clip = false;
    prt.dyna = false;
    prt.theta = 0;
    prt.side = 0;
    prt.name = NULL;

    /* assign one of the ports to every edge */
    for (i = 0; i < l->size; i++) {
	e = l->list[i];
	for (; e; e = ED_to_virt(e)) {	/* assign to all virt edges of e */
	    for (f = e; f;
		 f = ED_edge_type(f) == VIRTUAL &&
		 ND_node_type(aghead(f)) == VIRTUAL &&
		 ND_out(aghead(f)).size == 1 ?
		 ND_out(aghead(f)).list[0] : NULL) {
		if (aghead(f) == u)
		    ED_head_port(f) = prt;
		if (agtail(f) == u)
		    ED_tail_port(f) = prt;
	    }
	    for (f = e; f;
		 f = ED_edge_type(f) == VIRTUAL &&
		 ND_node_type(agtail(f)) == VIRTUAL &&
		 ND_in(agtail(f)).size == 1 ?
		 ND_in(agtail(f)).list[0] : NULL) {
		if (aghead(f) == u)
		    ED_head_port(f) = prt;
		if (agtail(f) == u)
		    ED_tail_port(f) = prt;
	    }
	}
    }

    ND_has_port(u) = true;	/* kinda pointless, because mincross is already done */
}
