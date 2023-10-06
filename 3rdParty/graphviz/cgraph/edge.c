/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "cghdr.h"

/* return first outedge of <n> */
Agedge_t *agfstout(Agraph_t * g, Agnode_t * n)
{
    Agsubnode_t *sn;
    Agedge_t *e = NULL;

    sn = agsubrep(g, n);
    if (sn) {
		dtrestore(g->e_seq, sn->out_seq);
		e = (Agedge_t*)dtfirst(g->e_seq);
		sn->out_seq = dtextract(g->e_seq);
	}
    return e;
}

/* return outedge that follows <e> of <n> */
Agedge_t *agnxtout(Agraph_t * g, Agedge_t * e)
{
    Agnode_t *n;
    Agsubnode_t *sn;
    Agedge_t *f = NULL;

    n = AGTAIL(e);
    sn = agsubrep(g, n);
    if (sn) {
		dtrestore(g->e_seq, sn->out_seq);
		f = (Agedge_t*)dtnext(g->e_seq, e);
		sn->out_seq = dtextract(g->e_seq);
	}
    return f;
}

Agedge_t *agfstin(Agraph_t * g, Agnode_t * n)
{
    Agsubnode_t *sn;
    Agedge_t *e = NULL;

    sn = agsubrep(g, n);
	if (sn) {
		dtrestore(g->e_seq, sn->in_seq);
		e = (Agedge_t*)dtfirst(g->e_seq);
		sn->in_seq = dtextract(g->e_seq);
	}
    return e;
}

Agedge_t *agnxtin(Agraph_t * g, Agedge_t * e)
{
    Agnode_t *n;
    Agsubnode_t *sn;
    Agedge_t *f = NULL;

    n = AGHEAD(e);
    sn = agsubrep(g, n);
	if (sn) {
		dtrestore(g->e_seq, sn->in_seq);
		f = (Agedge_t*)dtnext(g->e_seq, e);
		sn->in_seq = dtextract(g->e_seq);
	}
	return f;
}

Agedge_t *agfstedge(Agraph_t * g, Agnode_t * n)
{
    Agedge_t *rv;
    rv = agfstout(g, n);
    if (rv == NULL)
	rv = agfstin(g, n);
    return rv;
}

Agedge_t *agnxtedge(Agraph_t * g, Agedge_t * e, Agnode_t * n)
{
    Agedge_t *rv;

    if (AGTYPE(e) == AGOUTEDGE) {
	rv = agnxtout(g, e);
	if (rv == NULL) {
	    do {
		rv = !rv ? agfstin(g, n) : agnxtin(g,rv);
	    } while (rv && rv->node == n);
	}
    } else {
	do {
	    rv = agnxtin(g, e);		/* so that we only see each edge once, */
		e = rv;
	} while (rv && rv->node == n);	/* ignore loops as in-edges */
    }
    return rv;
}

/* internal edge set lookup */
static Agedge_t *agfindedge_by_key(Agraph_t * g, Agnode_t * t, Agnode_t * h,
			    Agtag_t key)
{
    Agedge_t *e, template_;
    Agsubnode_t *sn;

    if (t == NULL || h == NULL)
	return NULL;
    template_.base.tag = key;
    template_.node = t;		/* guess that fan-in < fan-out */
    sn = agsubrep(g, h);
    if (!sn) e = 0;
    else {
	    dtrestore(g->e_id, sn->in_id);
	    e = (Agedge_t*)dtsearch(g->e_id, &template_);
	    sn->in_id = dtextract(g->e_id);
    }
    return e;
}

static Agedge_t *agfindedge_by_id(Agraph_t * g, Agnode_t * t, Agnode_t * h,
                  IDTYPE id)
{
    Agtag_t tag = {0};

    tag.objtype = AGEDGE;
    tag.id = id;
    return agfindedge_by_key(g, t, h, tag);
}

Agsubnode_t *agsubrep(Agraph_t * g, Agnode_t * n)
{
    Agsubnode_t *sn, template_;

	if (g == n->root) sn = &(n->mainsub);
	else {
			template_.node = n;
			sn = (Agsubnode_t*)dtsearch(g->n_id, &template_);
	}
    return sn;
}

static void ins(Dict_t * d, Dtlink_t ** set, Agedge_t * e)
{
    dtrestore(d, *set);
    dtinsert(d, e);
    *set = dtextract(d);
}

static void del(Dict_t * d, Dtlink_t ** set, Agedge_t * e)
{
    void *x;
    (void)x;
    dtrestore(d, *set);
    x = dtdelete(d, e);
    assert(x);
    *set = dtextract(d);
}

static void installedge(Agraph_t * g, Agedge_t * e)
{
    Agnode_t *t, *h;
    Agedge_t *out, *in;
    Agsubnode_t *sn;

    out = AGMKOUT(e);
    in = AGMKIN(e);
    t = agtail(e);
    h = aghead(e);
    while (g) {
	if (agfindedge_by_key(g, t, h, AGTAG(e))) break;
	sn = agsubrep(g, t);
	ins(g->e_seq, &sn->out_seq, out);
	ins(g->e_id, &sn->out_id, out);
	sn = agsubrep(g, h);
	ins(g->e_seq, &sn->in_seq, in);
	ins(g->e_id, &sn->in_id, in);
	g = agparent(g);
    }
}

static void subedge(Agraph_t * g, Agedge_t * e)
{
    installedge(g, e);
    /* might an init method call be needed here? */
}

static Agedge_t *newedge(Agraph_t * g, Agnode_t * t, Agnode_t * h,
             IDTYPE id)
{
    Agedgepair_t *e2;
    Agedge_t *in, *out;

    (void)agsubnode(g,t,TRUE);
    (void)agsubnode(g,h,TRUE);
    e2 = (Agedgepair_t*)agalloc(g, sizeof(Agedgepair_t));
    in = &(e2->in);
    out = &(e2->out);
    uint64_t seq = agnextseq(g, AGEDGE);
    assert((seq & SEQ_MASK) == seq && "sequence ID overflow");
    AGTYPE(in) = AGINEDGE;
    AGTYPE(out) = AGOUTEDGE;
    AGID(in) = AGID(out) = id;
    AGSEQ(in) = AGSEQ(out) = seq & SEQ_MASK;
    in->node = t;
    out->node = h;

    installedge(g, out);
    if (g->desc.has_attrs) {
	(void)agbindrec(out, AgDataRecName, sizeof(Agattr_t), false);
	agedgeattr_init(g, out);
    }
    agmethod_init(g, out);
    return out;
}

/* edge creation predicate */
static int ok_to_make_edge(Agraph_t * g, Agnode_t * t, Agnode_t * h)
{
    Agtag_t key = {0};

    /* protect against self, multi-edges in strict graphs */
    if (agisstrict(g)) {
	key.objtype = 0;	/* wild card */
	if (agfindedge_by_key(g, t, h, key))
	    return FALSE;
    }
    if (g->desc.no_loop && (t == h)) /* simple graphs */
	return FALSE;
    return TRUE;
}

Agedge_t *agidedge(Agraph_t * g, Agnode_t * t, Agnode_t * h,
           IDTYPE id, int cflag)
{
    Agraph_t *root;
    Agedge_t *e;

    e = agfindedge_by_id(g, t, h, id);
    if (e == NULL && agisundirected(g))
	e = agfindedge_by_id(g, h, t, id);
    if (e == NULL && cflag && ok_to_make_edge(g, t, h)) {
	root = agroot(g);
	if (g != root && ((e = agfindedge_by_id(root, t, h, id)))) {
	    subedge(g, e);	/* old */
	} else {
	    if (agallocid(g, AGEDGE, id)) {
		e = newedge(g, t, h, id);	/* new */
	    }
	}
    }
    return e;
}

Agedge_t *agedge(Agraph_t * g, Agnode_t * t, Agnode_t * h, char *name,
		 int cflag)
{
    Agedge_t *e;
    IDTYPE my_id;
    int have_id;

    have_id = agmapnametoid(g, AGEDGE, name, &my_id, FALSE);
    if (have_id || (name == NULL && (!cflag || agisstrict(g)))) {
	/* probe for pre-existing edge */
	Agtag_t key = {0};
	if (have_id) {
	    key.id = my_id;
	    key.objtype = AGEDGE;
	} else {
	    key.id = key.objtype = 0;
	}

	/* might already exist locally */
	e = agfindedge_by_key(g, t, h, key);
	if (e == NULL && agisundirected(g))
	    e = agfindedge_by_key(g, h, t, key);
	if (e)
	    return e;
	if (cflag) {
	    e = agfindedge_by_key(agroot(g), t, h, key);
	    if (e == NULL && agisundirected(g))
		e = agfindedge_by_key(agroot(g), h, t, key);
	    if (e) {
		subedge(g,e);
		return e;
	    }
	}
    }

    if (cflag && ok_to_make_edge(g, t, h)
	&& agmapnametoid(g, AGEDGE, name, &my_id, TRUE)) { /* reserve id */
	e = newedge(g, t, h, my_id);
	agregister(g, AGEDGE, e); /* register new object in external namespace */
    }
    else
	e = NULL;
    return e;
}

void agdeledgeimage(Agraph_t * g, Agedge_t * e, void *ignored)
{
    Agedge_t *in, *out;
    Agnode_t *t, *h;
    Agsubnode_t *sn;

    (void)ignored;
    if (AGTYPE(e) == AGINEDGE) {
	in = e;
	out = AGIN2OUT(e);
    } else {
	out = e;
	in = AGOUT2IN(e);
    }
    t = in->node;
    h = out->node;
    sn = agsubrep(g, t);
    del(g->e_seq, &sn->out_seq, out);
    del(g->e_id, &sn->out_id, out);
    sn = agsubrep(g, h);
    del(g->e_seq, &sn->in_seq, in);
    del(g->e_id, &sn->in_id, in);
#ifdef DEBUG
    for (e = agfstin(g,h); e; e = agnxtin(g,e))
	assert(e != in);
    for (e = agfstout(g,t); e; e = agnxtout(g,e))
	assert(e != out);
#endif
}

int agdeledge(Agraph_t * g, Agedge_t * e)
{
    e = AGMKOUT(e);
    if (agfindedge_by_key(g, agtail(e), aghead(e), AGTAG(e)) == NULL)
	return FAILURE;

    if (g == agroot(g)) {
	if (g->desc.has_attrs)
	    agedgeattr_delete(e);
	agmethod_delete(g, e);
	agrecclose((Agobj_t *) e);
	agfreeid(g, AGEDGE, AGID(e));
    }
    if (agapply (g, (Agobj_t *) e, (agobjfn_t) agdeledgeimage, NULL, FALSE) == SUCCESS) {
	if (g == agroot(g))
		agfree(g, e);
	return SUCCESS;
    } else
	return FAILURE;
}

Agedge_t *agsubedge(Agraph_t * g, Agedge_t * e, int cflag)
{
    Agnode_t *t, *h;
    Agedge_t *rv;

    rv = NULL;
    t = agsubnode(g, AGTAIL(e), cflag);
    h = agsubnode(g, AGHEAD(e), cflag);
    if (t && h) {
	rv = agfindedge_by_key(g, t, h, AGTAG(e));
	if (cflag && rv == NULL) {
	installedge(g, e);
	rv = e;
	}
	if (rv && (AGTYPE(rv) != AGTYPE(e)))
	    rv = AGOPP(rv);
    }
    return rv;
}

/* edge comparison.  AGTYPE(e) == 0 means ID is a wildcard. */
static int agedgeidcmpf(Dict_t * d, void *arg_e0, void *arg_e1, Dtdisc_t * disc)
{
    Agedge_t *e0, *e1;

    (void)d;
    e0 = (Agedge_t*)arg_e0;
    e1 = (Agedge_t*)arg_e1;
    (void)disc;

    if (AGID(e0->node) < AGID(e1->node)) return -1;
    if (AGID(e0->node) > AGID(e1->node)) return 1;
    /* same node */
    if (AGTYPE(e0) != 0 && AGTYPE(e1) != 0) {
        if (AGID(e0) < AGID(e1)) return -1;
        if (AGID(e0) > AGID(e1)) return 1;
    }
    return 0;
}

/* edge comparison.  for ordered traversal. */
static int agedgeseqcmpf(Dict_t * d, void *arg_e0, void *arg_e1, Dtdisc_t * disc)
{
    Agedge_t *e0, *e1;

    (void)d;
    e0 = (Agedge_t*)arg_e0;
    e1 = (Agedge_t*)arg_e1;
    (void)disc;
    assert(arg_e0 && arg_e1);

    if (e0->node != e1->node) {
        if (AGSEQ(e0->node) < AGSEQ(e1->node)) return -1;
        if (AGSEQ(e0->node) > AGSEQ(e1->node)) return 1;
    }
    else {
        if (AGSEQ(e0) < AGSEQ(e1)) return -1;
        if (AGSEQ(e0) > AGSEQ(e1)) return 1;
    }
    return 0;
}

/* indexing for ordered traversal */
Dtdisc_t Ag_mainedge_seq_disc = {};

Dtdisc_t Ag_subedge_seq_disc = {};

/* indexing for random search */
Dtdisc_t Ag_mainedge_id_disc = {};

Dtdisc_t Ag_subedge_id_disc = {};

struct Initialiser3
{
    Initialiser3()
    {
    Ag_mainedge_seq_disc.link = offsetof(Agedge_t, seq_link);
    Ag_mainedge_seq_disc.comparf = agedgeseqcmpf;
    Ag_mainedge_seq_disc.memoryf = agdictobjmem;

    Ag_subedge_seq_disc.link = -1;
    Ag_subedge_seq_disc.comparf = agedgeseqcmpf;
    Ag_subedge_seq_disc.memoryf = agdictobjmem;

    Ag_mainedge_id_disc.link = offsetof(Agedge_t, id_link);
    Ag_mainedge_id_disc.comparf = agedgeidcmpf;
    Ag_mainedge_id_disc.memoryf = agdictobjmem;

    Ag_subedge_id_disc.link = -1;
    Ag_subedge_id_disc.comparf = agedgeidcmpf;
    Ag_subedge_id_disc.memoryf = agdictobjmem;
    }
};

static Initialiser3 initialiser3;