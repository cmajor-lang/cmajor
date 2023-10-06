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

typedef struct IMapEntry_s {
    Dtlink_t namedict_link;
    Dtlink_t iddict_link;
    IDTYPE id;
    char *str;
} IMapEntry_t;

static int idcmpf(Dict_t * d, void *arg_p0, void *arg_p1, Dtdisc_t * disc)
{
    IMapEntry_t *p0, *p1;

    (void)d;
    p0 = (IMapEntry_t*)arg_p0;
    p1 = (IMapEntry_t*)arg_p1;
    (void)disc;
    if (p0->id > p1->id)
    {
        return 1;
    }
    else if (p0->id < p1->id)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

/* note, OK to compare pointers into shared string pool
 * but can't probe with an arbitrary string pointer
 */
static int namecmpf(Dict_t * d, void *arg_p0, void *arg_p1,
		    Dtdisc_t * disc)
{
    IMapEntry_t *p0, *p1;

    (void)d;
    p0 = (IMapEntry_t*)arg_p0;
    p1 = (IMapEntry_t*)arg_p1;
    (void)disc;
    if (p0->str > p1->str)
    {
        return 1;
    }
    else if (p0->str < p1->str)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

static Dtdisc_t LookupByName = {
    0,				/* object ptr is passed as key */
    0,				/* size (ignored) */
    offsetof(IMapEntry_t, namedict_link),
    NULL,
    NULL,
    namecmpf,
    NULL,
    agdictobjmem,
    NULL
};

static Dtdisc_t LookupById = {
    0,				/* object ptr is passed as key */
    0,				/* size (ignored) */
    offsetof(IMapEntry_t, iddict_link),
    NULL,
    NULL,
    idcmpf,
    NULL,
    agdictobjmem,
    NULL
};

int aginternalmaplookup(Agraph_t * g, int objtype, char *str,
            IDTYPE *result)
{
    Dict_t *d;
    IMapEntry_t *sym, template_;
    char *search_str;

    if (objtype == AGINEDGE)
	objtype = AGEDGE;
    if ((d = g->clos->lookup_by_name[objtype])) {
	if ((search_str = agstrbind(g, str))) {
	    template_.str = search_str;
	    sym = (IMapEntry_t*)dtsearch(d, &template_);
	    if (sym) {
		*result = sym->id;
		return TRUE;
	    }
	}
    }
    return FALSE;
}

/* caller GUARANTEES that this is a new entry */
void aginternalmapinsert(Agraph_t * g, int objtype, char *str,
             IDTYPE id)
{
    IMapEntry_t *ent;
    Dict_t *d_name_to_id, *d_id_to_name;

    ent = AGNEW(g, IMapEntry_t);
    ent->id = id;
    ent->str = agstrdup(g, str);

    if (objtype == AGINEDGE)
	objtype = AGEDGE;
    if ((d_name_to_id = g->clos->lookup_by_name[objtype]) == NULL)
	d_name_to_id = g->clos->lookup_by_name[objtype] =
	    agdtopen(g, &LookupByName, Dttree);
    if ((d_id_to_name = g->clos->lookup_by_id[objtype]) == NULL)
	d_id_to_name = g->clos->lookup_by_id[objtype] =
	    agdtopen(g, &LookupById, Dttree);
    dtinsert(d_name_to_id, ent);
    dtinsert(d_id_to_name, ent);
}

static IMapEntry_t *find_isym(Agraph_t * g, int objtype, IDTYPE id)
{
    Dict_t *d;
    IMapEntry_t *isym, itemplate_;

    if (objtype == AGINEDGE)
	objtype = AGEDGE;
    if ((d = g->clos->lookup_by_id[objtype])) {
	itemplate_.id = id;
	isym = (IMapEntry_t*)dtsearch(d, &itemplate_);
    } else
	isym = NULL;
    return isym;
}

char *aginternalmapprint(Agraph_t * g, int objtype, IDTYPE id)
{
    IMapEntry_t *isym;

    if ((isym = find_isym(g, objtype, id)))
	return isym->str;
    return NULL;
}


int aginternalmapdelete(Agraph_t * g, int objtype, IDTYPE id)
{
    IMapEntry_t *isym;

    if (objtype == AGINEDGE)
	objtype = AGEDGE;
    if ((isym = find_isym(g, objtype, id))) {
	dtdelete(g->clos->lookup_by_name[objtype], isym);
	dtdelete(g->clos->lookup_by_id[objtype], isym);
	agstrfree(g, isym->str);
	agfree(g, isym);
	return TRUE;
    }
    return FALSE;
}

void aginternalmapclearlocalnames(Agraph_t * g)
{
    int i;
    IMapEntry_t *sym, *nxt;
    Dict_t **d_name;

    Ag_G_global = g;
    d_name = g->clos->lookup_by_name;
    for (i = 0; i < 3; i++) {
	if (d_name[i]) {
	    for (sym = (IMapEntry_t*)dtfirst(d_name[i]); sym; sym = nxt) {
		nxt = (IMapEntry_t*)dtnext(d_name[i], sym);
		if (sym->str[0] == LOCALNAMEPREFIX)
		    aginternalmapdelete(g, i, sym->id);
	    }
	}
    }
}

static void closeit(Dict_t ** d)
{
    int i;

    for (i = 0; i < 3; i++) {
	if (d[i]) {
	    dtclose(d[i]);
	    d[i] = NULL;
	}
    }
}

void aginternalmapclose(Agraph_t * g)
{
    Ag_G_global = g;
    closeit(g->clos->lookup_by_name);
    closeit(g->clos->lookup_by_id);
}
