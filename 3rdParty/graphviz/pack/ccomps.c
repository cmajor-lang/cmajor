/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/


#include "../cgraph/agxbuf.h"
#include "../cgraph/alloc.h"
#include "../cgraph/prisize_t.h"
#include "../cgraph/stack.h"
#include "../common/render.h"
#include "../pack/pack.h"

#define MARKED(stk,n) ((stk)->markfn(n,-1))
#define MARK(stk,n)   ((stk)->markfn(n,1))
#define UNMARK(stk,n) ((stk)->markfn(n,0))

typedef struct {
    gv_stack_t data;
    void (*actionfn) (Agnode_t *, void *);
    int (*markfn) (Agnode_t *, int);
} stk_t;

static void initStk(stk_t *sp, void (*actionfn)(Agnode_t*, void*),
     int (*markfn) (Agnode_t *, int))
{
    sp->data = gv_stack_t{0};
    sp->actionfn = actionfn;
    sp->markfn = markfn;
}

static void freeStk (stk_t* sp)
{
  stack_reset(&sp->data);
}

static int push(stk_t* sp, Agnode_t * np)
{
  MARK(sp, np);
  return stack_push(&sp->data, np);
}

static Agnode_t *pop(stk_t* sp)
{
  if (stack_is_empty(&sp->data)) {
    return NULL;
  }

  return (Agnode_t *)stack_pop(&sp->data);
}


static size_t dfs(Agraph_t * g, Agnode_t * n, void *state, stk_t* stk)
{
    Agedge_t *e;
    Agnode_t *other;
    size_t cnt = 0;

    if (push (stk, n) != 0) {
	return SIZE_MAX;
    }
    while ((n = pop(stk))) {
	cnt++;
	if (stk->actionfn) stk->actionfn(n, state);
        for (e = agfstedge(g, n); e; e = agnxtedge(g, e, n)) {
	    if ((other = agtail(e)) == n)
		other = aghead(e);
            if (!MARKED(stk,other))
                if (push(stk, other) != 0) {
                    return SIZE_MAX;
                }
        }
    }
    return cnt;
}

static int isLegal(const char *p) {
    char c;

    while ((c = *p++)) {
	if (c != '_' && !isalnum((int)c))
	    return 0;
    }

    return 1;
}

static void insertFn(Agnode_t * n, void *state)
{
    agsubnode((Agraph_t *)state, n, 1);
}

static int markFn (Agnode_t* n, int v)
{
    int ret;
    if (v < 0) return ND_mark(n);
    ret = ND_mark(n);
    ND_mark(n) = (char) v;
    return ret;
}

static void setPrefix(agxbuf *xb, const char *pfx) {
    if (!pfx || !isLegal(pfx)) {
        pfx = "_cc_";
    }
    agxbput(xb, pfx);
}

/* pccomps:
 * Return an array of subgraphs consisting of the connected
 * components of graph g. The number of components is returned in ncc.
 * All pinned nodes are in one component.
 * If pfx is non-null and a legal graph name, we use it as the prefix
 * for the name of the subgraphs created. If not, a simple default is used.
 * If pinned is non-null, *pinned set to 1 if pinned nodes found
 * and the first component is the one containing the pinned nodes.
 * Note that the component subgraphs do not contain any edges. These must
 * be obtained from the root graph.
 * Return NULL on error or if graph is empty.
 */
Agraph_t **pccomps(Agraph_t * g, int *ncc, char *pfx, bool *pinned)
{
    size_t c_cnt = 0;
    agxbuf name = {0};
    Agraph_t *out = NULL;
    Agnode_t *n;
    size_t bnd = 10;
    bool pin = false;
    stk_t stk;
    int error = 0;

    if (agnnodes(g) == 0) {
	*ncc = 0;
	return NULL;
    }

    Agraph_t **ccs = (Agraph_t **)gv_calloc(bnd, sizeof(Agraph_t*));

    initStk(&stk, insertFn, markFn);
    for (n = agfstnode(g); n; n = agnxtnode(g, n))
	UNMARK(&stk,n);

    /* Component with pinned nodes */
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (MARKED(&stk,n) || !isPinned(n))
	    continue;
	if (!out) {
	    setPrefix(&name, pfx);
	    agxbprint(&name, "%" PRISIZE_T, c_cnt);
	    out = agsubg(g, agxbuse(&name),1);
	    agbindrec(out, "Agraphinfo_t", sizeof(Agraphinfo_t), true);	//node custom data
	    ccs[c_cnt] = out;
	    c_cnt++;
	    pin = true;
	}
	if (dfs (g, n, out, &stk) == SIZE_MAX) {
	    error = 1;
	    goto packerror;
	}
    }

    /* Remaining nodes */
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (MARKED(&stk,n))
	    continue;
	setPrefix(&name, pfx);
	agxbprint(&name, "%" PRISIZE_T, c_cnt);
	out = agsubg(g, agxbuse(&name), 1);
	agbindrec(out, "Agraphinfo_t", sizeof(Agraphinfo_t), true);	//node custom data
	if (dfs(g, n, out, &stk) == SIZE_MAX) {
	    error = 1;
	    goto packerror;
	}
	if (c_cnt == bnd) {
	    ccs = (Agraph_t **)gv_recalloc(ccs, bnd, bnd * 2, sizeof(Agraph_t*));
	    bnd *= 2;
	}
	ccs[c_cnt] = out;
	c_cnt++;
    }
packerror:
    freeStk (&stk);
    agxbfree(&name);
    if (error) {
	*ncc = 0;
	for (size_t i=0; i < c_cnt; i++) {
	    agclose (ccs[i]);
	}
	free (ccs);
	ccs = NULL;
    }
    else {
	ccs = (Agraph_t **) gv_recalloc(ccs, bnd, c_cnt, sizeof(Agraph_t*));
	*ncc = (int) c_cnt;
	*pinned = pin;
    }
    return ccs;
}

/* ccomps:
 * Return an array of subgraphs consisting of the connected
 * components of graph g. The number of components is returned in ncc.
 * If pfx is non-null and a legal graph name, we use it as the prefix
 * for the name of the subgraphs created. If not, a simple default is used.
 * Note that the component subgraphs do not contain any edges. These must
 * be obtained from the root graph.
 * Returns NULL on error or if graph is empty.
 */
Agraph_t **ccomps(Agraph_t * g, int *ncc, char *pfx)
{
    size_t c_cnt = 0;
    agxbuf name = {0};
    Agraph_t *out;
    Agnode_t *n;
    size_t bnd = 10;
    stk_t stk;

    if (agnnodes(g) == 0) {
	*ncc = 0;
	return NULL;
    }

    Agraph_t **ccs = (Agraph_t **)gv_calloc(bnd, sizeof(Agraph_t*));
    initStk(&stk, insertFn, markFn);
    for (n = agfstnode(g); n; n = agnxtnode(g, n))
	UNMARK(&stk,n);

    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (MARKED(&stk,n))
	    continue;
	setPrefix(&name, pfx);
	agxbprint(&name, "%" PRISIZE_T, c_cnt);
	out = agsubg(g, agxbuse(&name), 1);
	agbindrec(out, "Agraphinfo_t", sizeof(Agraphinfo_t), true);	//node custom data
	if (dfs(g, n, out, &stk) == SIZE_MAX) {
	    freeStk (&stk);
	    free (ccs);
	    agxbfree(&name);
	    *ncc = 0;
	    return NULL;
	}
	if (c_cnt == bnd) {
	    ccs = (Agraph_t **)gv_recalloc(ccs, bnd, bnd * 2, sizeof(Agraph_t*));
	    bnd *= 2;
	}
	ccs[c_cnt] = out;
	c_cnt++;
    }
    freeStk (&stk);
    ccs = (Agraph_t **)gv_recalloc(ccs, bnd, c_cnt, sizeof(Agraph_t*));
    agxbfree(&name);
    *ncc = (int) c_cnt;
    return ccs;
}

typedef struct {
    Agrec_t h;
    char cc_subg;   /* true iff subgraph corresponds to a component */
} ccgraphinfo_t;

typedef struct {
    Agrec_t h;
    char mark;
    union {
	Agraph_t* g;
	Agnode_t* n;
	void*     v;
    } ptr;
} ccgnodeinfo_t;

#define GRECNAME "ccgraphinfo"
#define NRECNAME "ccgnodeinfo"
#define GD_cc_subg(g)  (((ccgraphinfo_t*)aggetrec(g, GRECNAME, FALSE))->cc_subg)
#ifdef DEBUG
Agnode_t*
dnodeOf (Agnode_t* v)
{
  ccgnodeinfo_t* ip = (ccgnodeinfo_t*)aggetrec(v, NRECNAME, FALSE);
  if (ip)
    return ip->ptr.n;
  fprintf (stderr, "nodeinfo undefined\n");
  return NULL;
}
void
dnodeSet (Agnode_t* v, Agnode_t* n)
{
  ((ccgnodeinfo_t*)aggetrec(v, NRECNAME, FALSE))->ptr.n = n;
}
#else
#define dnodeOf(v)  (((ccgnodeinfo_t*)aggetrec(v, NRECNAME, FALSE))->ptr.n)
#define dnodeSet(v,w) (((ccgnodeinfo_t*)aggetrec(v, NRECNAME, FALSE))->ptr.n=w)
#endif

#define ptrOf(np)  (((ccgnodeinfo_t*)((np)->base.data))->ptr.v)
#define nodeOf(np)  (((ccgnodeinfo_t*)((np)->base.data))->ptr.n)
#define clustOf(np)  (((ccgnodeinfo_t*)((np)->base.data))->ptr.g)
#define clMark(n) (((ccgnodeinfo_t*)(n->base.data))->mark)

/* isCluster:
 * Return true if graph is a cluster
 */
#define isCluster(g) (strncmp(agnameof(g), "cluster", 7) == 0)

/* deriveClusters:
 * Construct nodes in derived graph corresponding top-level clusters.
 * Since a cluster might be wrapped in a subgraph, we need to traverse
 * down into the tree of subgraphs
 */
static void deriveClusters(Agraph_t* dg, Agraph_t * g)
{
    Agraph_t *subg;
    Agnode_t *dn;
    Agnode_t *n;

    for (subg = agfstsubg(g); subg; subg = agnxtsubg(subg)) {
	if (isCluster(subg)) {
	    dn = agnode(dg, agnameof(subg), 1);
	    agbindrec (dn, NRECNAME, sizeof(ccgnodeinfo_t), true);
	    clustOf(dn) = subg;
	    for (n = agfstnode(subg); n; n = agnxtnode(subg, n)) {
		if (dnodeOf(n)) {
		   fprintf (stderr, "Error: node \"%s\" belongs to two non-nested clusters \"%s\" and \"%s\"\n",
			agnameof (n), agnameof(subg), agnameof(dnodeOf(n)));
		}
		dnodeSet(n,dn);
	    }
	}
	else {
	    deriveClusters (dg, subg);
	}
    }
}

/* deriveGraph:
 * Create derived graph dg of g where nodes correspond to top-level nodes
 * or clusters, and there is an edge in dg if there is an edge in g
 * between any nodes in the respective clusters.
 */
static Agraph_t *deriveGraph(Agraph_t * g)
{
    Agraph_t *dg;
    Agnode_t *dn;
    Agnode_t *n;

    dg = agopen((char*) "dg", Agstrictundirected, NULL);

    deriveClusters (dg, g);

    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (dnodeOf(n))
	    continue;
	dn = agnode(dg, agnameof(n), 1);
	agbindrec (dn, NRECNAME, sizeof(ccgnodeinfo_t), true);
	nodeOf(dn) = n;
	dnodeSet(n,dn);
    }

    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	Agedge_t *e;
	Agnode_t *hd;
	Agnode_t *tl = dnodeOf(n);
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
	    hd = aghead(e);
	    hd = dnodeOf(hd);
	    if (hd == tl)
		continue;
	    if (hd > tl)
		agedge(dg, tl, hd, NULL, 1);
	    else
		agedge(dg, hd, tl, NULL, 1);
	}
    }

    return dg;
}

/* unionNodes:
 * Add all nodes in cluster nodes of dg to g
 */
static void unionNodes(Agraph_t * dg, Agraph_t * g)
{
    Agnode_t *n;
    Agnode_t *dn;
    Agraph_t *clust;

    for (dn = agfstnode(dg); dn; dn = agnxtnode(dg, dn)) {
	if (AGTYPE(ptrOf(dn)) == AGNODE) {
	    agsubnode(g, nodeOf(dn), 1);
	} else {
	    clust = clustOf(dn);
	    for (n = agfstnode(clust); n; n = agnxtnode(clust, n))
		agsubnode(g, n, 1);
	}
    }
}

static int clMarkFn (Agnode_t* n, int v)
{
    int ret;
    if (v < 0) return clMark(n);
    ret = clMark(n);
    clMark(n) = (char) v;
    return ret;
}

/* node_induce_:
 * Using the edge set of eg, add to g any edges
 * with both endpoints in g.
 * Returns the number of edges added.
 */
static int node_induce_(Agraph_t * g, Agraph_t* eg)
{
    Agnode_t *n;
    Agedge_t *e;
    int e_cnt = 0;

    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	for (e = agfstout(eg, n); e; e = agnxtout(eg, e)) {
	    if (agsubnode(g, aghead(e),0)) {
		agsubedge(g,e,1);
		e_cnt++;
	    }
	}
    }
    return e_cnt;
}


typedef struct {
    Agrec_t h;
    Agraph_t* orig;
} orig_t;

#define ORIG_REC "orig"

Agraph_t*
mapClust(Agraph_t *cl)
{
    orig_t* op = (orig_t*)aggetrec(cl, ORIG_REC, 0);
    assert (op);
    return op->orig;
}

/* projectG:
 * If any nodes of subg are in g, create a subgraph of g
 * and fill it with all nodes of subg in g and their induced
 * edges in subg. Copy the attributes of subg to g. Return the subgraph.
 * If not, return null.
 * If subg is a cluster, the new subgraph will contain a pointer to it
 * in the record "orig".
 */
static Agraph_t *projectG(Agraph_t * subg, Agraph_t * g, int inCluster)
{
    Agraph_t *proj = NULL;
    Agnode_t *n;
    Agnode_t *m;
    orig_t *op;

    for (n = agfstnode(subg); n; n = agnxtnode(subg, n)) {
	if ((m = agfindnode(g, agnameof(n)))) {
	    if (proj == NULL) {
		proj = agsubg(g, agnameof(subg), 1);
	    }
	    agsubnode(proj, m, 1);
	}
    }
    if (!proj && inCluster) {
	proj = agsubg(g, agnameof(subg), 1);
    }
    if (proj) {
	node_induce_(proj, subg);
	agcopyattr(subg, proj);
	if (isCluster(proj)) {
	    op = (orig_t*)agbindrec(proj,ORIG_REC, sizeof(orig_t), false);
	    op->orig = subg;
	}
    }

    return proj;
}

/* subgInduce:
 * Project subgraphs of root graph on subgraph.
 * If non-empty, add to subgraph.
 */
static void
subgInduce(Agraph_t * root, Agraph_t * g, int inCluster)
{
    Agraph_t *subg;
    Agraph_t *proj;
    int in_cluster;

/* fprintf (stderr, "subgInduce %s inCluster %d\n", agnameof(root), inCluster); */
    for (subg = agfstsubg(root); subg; subg = agnxtsubg(subg)) {
	if (GD_cc_subg(subg))
	    continue;
	if ((proj = projectG(subg, g, inCluster))) {
	    in_cluster = (inCluster || isCluster(subg));
	    subgInduce(subg, proj, in_cluster);
	}
    }
}

static void
subGInduce(Agraph_t* g, Agraph_t * out)
{
    subgInduce(g, out, 0);
}

/* cccomps:
 * Decompose g into "connected" components, where nodes are connected
 * either by an edge or by being in the same cluster. The components
 * are returned in an array of subgraphs. ncc indicates how many components
 * there are. The subgraphs use the prefix pfx in their names, if non-NULL.
 * Note that cluster subgraph of the main graph, corresponding to a component,
 * is cloned within the subgraph. Each cloned cluster contains a record pointing
 * to the real cluster.
 */
Agraph_t **cccomps(Agraph_t * g, int *ncc, char *pfx)
{
    Agraph_t *dg;
    size_t n_cnt, c_cnt, e_cnt;
    agxbuf name = {0};
    Agraph_t *out;
    Agraph_t *dout;
    Agnode_t *dn;
    stk_t stk;
    int sz = (int) sizeof(ccgraphinfo_t);

    if (agnnodes(g) == 0) {
	*ncc = 0;
	return NULL;
    }

    /* Bind ccgraphinfo to graph and all subgraphs */
    aginit(g, AGRAPH, GRECNAME, -sz, FALSE);

    /* Bind ccgraphinfo to graph and all subgraphs */
    aginit(g, AGNODE, NRECNAME, sizeof(ccgnodeinfo_t), FALSE);

    dg = deriveGraph(g);

    size_t ccs_length = (size_t)agnnodes(dg);
    Agraph_t **ccs = (Agraph_t **) gv_calloc(ccs_length, sizeof(Agraph_t*));
    initStk(&stk, insertFn, clMarkFn);

    c_cnt = 0;
    for (dn = agfstnode(dg); dn; dn = agnxtnode(dg, dn)) {
	if (MARKED(&stk,dn))
	    continue;
	setPrefix(&name, pfx);
	agxbprint(&name, "%" PRISIZE_T, c_cnt);
	char *name_str = agxbuse(&name);
	dout = agsubg(dg, name_str, 1);
	out = agsubg(g, name_str, 1);
	agbindrec(out, GRECNAME, sizeof(ccgraphinfo_t), false);
	GD_cc_subg(out) = 1;
	n_cnt = dfs(dg, dn, dout, &stk);
	if (n_cnt == SIZE_MAX) {
	    agclose(dg);
	    agclean (g, AGRAPH, (char*) GRECNAME);
	    agclean (g, AGNODE, (char*) NRECNAME);
	    freeStk (&stk);
	    free(ccs);
	    agxbfree(&name);
	    *ncc = 0;
	    return NULL;
	}
	unionNodes(dout, out);
	e_cnt = (size_t) nodeInduce(out);
	subGInduce(g, out);
	ccs[c_cnt] = out;
	agdelete(dg, dout);
	if (Verbose)
	    fprintf(stderr, "(%4" PRISIZE_T ") %7" PRISIZE_T " nodes %7" PRISIZE_T
	            " edges\n", c_cnt, n_cnt, e_cnt);
	c_cnt++;
    }

    if (Verbose)
	fprintf(stderr, "       %7d nodes %7d edges %7" PRISIZE_T " components %s\n",
	    agnnodes(g), agnedges(g), c_cnt, agnameof(g));

    agclose(dg);
    agclean (g, AGRAPH, (char*) GRECNAME);
    agclean (g, AGNODE, (char*) NRECNAME);
    freeStk (&stk);
    ccs = (Agraph_t **)gv_recalloc(ccs, ccs_length, c_cnt, sizeof(Agraph_t*));
    agxbfree(&name);
    *ncc = (int) c_cnt;
    return ccs;
}

/* isConnected:
 * Returns 1 if the graph is connected.
 * Returns 0 if the graph is not connected.
 * Returns -1 if the graph is error.
 */
int isConnected(Agraph_t * g)
{
    Agnode_t *n;
    int ret = 1;
    size_t cnt = 0;
    stk_t stk;

    if (agnnodes(g) == 0)
	return 1;

    initStk(&stk, NULL, markFn);
    for (n = agfstnode(g); n; n = agnxtnode(g, n))
	UNMARK(&stk,n);

    n = agfstnode(g);
    cnt = dfs(g, agfstnode(g), NULL, &stk);
    freeStk (&stk);
    if (cnt == SIZE_MAX) { /* dfs failed */
	return -1;
    }
    if (cnt != (size_t) agnnodes(g))
	ret = 0;
    return ret;
}

/* nodeInduce:
 * Given a subgraph, adds all edges in the root graph both of whose
 * endpoints are in the subgraph.
 * If g is a connected component, this will be all edges attached to
 * any node in g.
 * Returns the number of edges added.
 */
int nodeInduce(Agraph_t * g)
{
    return node_induce_ (g, g->root);
}
