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
#include "likely.h"

/*
 * reference counted strings.
 */

typedef struct {
    Dtlink_t link;
    uint64_t refcnt: sizeof(uint64_t) * 8 - 1;
    uint64_t is_html: 1;
    char *s;
    char store[1];		/* this is actually a dynamic array */
} refstr_t;

static Dtdisc_t Refstrdisc = {
    offsetof(refstr_t, s),	/* key */
    -1,				/* size */
    0,				/* link offset */
    NULL,
    agdictobjfree,
    NULL,
    NULL,
    agdictobjmem,
    NULL
};

static Dict_t *Refdict_default;

/* refdict:
 * Return the string dictionary associated with g.
 * If necessary, create it.
 * As a side-effect, set html masks. This assumes 8-bit bytes.
 */
static Dict_t *refdict(Agraph_t * g)
{
    Dict_t **dictref;

    if (g)
	dictref = &(g->clos->strdict);
    else
	dictref = &Refdict_default;
    if (*dictref == NULL) {
	*dictref = agdtopen(g, &Refstrdisc, Dttree);
    }
    return *dictref;
}

int agstrclose(Agraph_t * g)
{
    return agdtclose(g, refdict(g));
}

static refstr_t *refsymbind(Dict_t * strdict, const char *s)
{
    refstr_t key, *r;
// Suppress Clang/GCC -Wcast-qual warning. Casting away const here is acceptable
// as dtsearch does not modify its input key.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
    key.s = (char*)s;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    r = (refstr_t*)dtsearch(strdict, &key);
    return r;
}

static char *refstrbind(Dict_t * strdict, const char *s)
{
    refstr_t *r;
    r = refsymbind(strdict, s);
    if (r)
	return r->s;
    else
	return NULL;
}

char *agstrbind(Agraph_t * g, const char *s)
{
    return refstrbind(refdict(g), s);
}

static char *agstrdup_internal(Agraph_t *g, const char *s, bool is_html) {
    refstr_t *r;
    Dict_t *strdict;
    size_t sz;

    if (s == NULL)
	 return NULL;
    strdict = refdict(g);
    r = refsymbind(strdict, s);
    if (r)
	r->refcnt++;
    else {
	sz = sizeof(refstr_t) + strlen(s);
	if (g)
	    r = (refstr_t*)agalloc(g, sz);
	else {
	    r = (refstr_t*)malloc(sz);
	    if (UNLIKELY(sz > 0 && r == NULL)) {
	        return NULL;
	    }
	}
	r->refcnt = 1;
	r->is_html = is_html;
	strcpy(r->store, s);
	r->s = r->store;
	dtinsert(strdict, r);
    }
    return r->s;
}

char *agstrdup(Agraph_t *g, const char *s) {
  return agstrdup_internal(g, s, false);
}

char *agstrdup_html(Agraph_t *g, const char *s) {
  return agstrdup_internal(g, s, true);
}

int agstrfree(Agraph_t * g, const char *s)
{
    refstr_t *r;
    Dict_t *strdict;

    if (s == NULL)
	 return FAILURE;

    strdict = refdict(g);
    r = refsymbind(strdict, s);
    if (r && (r->s == s)) {
	r->refcnt--;
	if (r->refcnt == 0) {
	    agdtdelete(g, strdict, r);
	}
    }
    if (r == NULL)
	return FAILURE;
    return SUCCESS;
}

/* aghtmlstr:
 * Return true if s is an HTML string.
 * We assume s points to the datafield store[0] of a refstr.
 */
int aghtmlstr(const char *s)
{
    const refstr_t *key;

    if (s == NULL)
	return 0;
    key = (const refstr_t *) (s - offsetof(refstr_t, store[0]));
    return key->is_html;
}

void agmarkhtmlstr(char *s)
{
    refstr_t *key;

    if (s == NULL)
	return;
    key = (refstr_t *) (s - offsetof(refstr_t, store[0]));
    key->is_html = 1;
}

#ifdef DEBUG
static int refstrprint(Dict_t * dict, void *ptr, void *user)
{
    refstr_t *r;

    (void)dict;
    r = ptr;
    (void)user;
    fprintf(stderr, "%s\n", r->s);
    return 0;
}

void agrefstrdump(Agraph_t * g)
{
    dtwalk(refdict(g), refstrprint, 0);
}
#endif
