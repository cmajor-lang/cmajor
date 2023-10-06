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

/* a default ID allocator that works off the shared string lib */

static void *idopen(Agraph_t * g, Agdisc_t* disc)
{
    (void)disc;
    return g;
}

static long idmap(void *state, int objtype, char *str, IDTYPE *id,
		  int createflag)
{
    char *s;
    static IDTYPE ctr = 1;

    (void)objtype;
    if (str) {
        Agraph_t *g;
        g = (Agraph_t*)state;
        if (createflag)
            s = agstrdup(g, str);
        else
            s = agstrbind(g, str);
        *id = (IDTYPE)(uintptr_t)s;
    } else {
        *id = ctr;
        ctr += 2;
    }
    return TRUE;
}

	/* we don't allow users to explicitly set IDs, either */
static long idalloc(void *state, int objtype, IDTYPE request)
{
    (void)state;
    (void)objtype;
    (void)request;
    return FALSE;
}

static void idfree(void *state, int objtype, IDTYPE id)
{
    (void)objtype;
    if (id % 2 == 0)
	agstrfree((Agraph_t*)state, (char *)(uintptr_t)id);
}

static char *idprint(void *state, int objtype, IDTYPE id)
{
    (void)state;
    (void)objtype;
    if (id % 2 == 0)
	return (char *)(uintptr_t)id;
    else
	return NULL;
}

static void idclose(void *state)
{
    (void)state;
}

static void idregister(void *state, int objtype, void *obj)
{
    (void)state;
    (void)objtype;
    (void)obj;
}

Agiddisc_t AgIdDisc = {
    idopen,
    idmap,
    idalloc,
    idfree,
    idprint,
    idclose,
    idregister
};

/* aux functions incl. support for disciplines with anonymous IDs */

int agmapnametoid(Agraph_t * g, int objtype, char *str,
          IDTYPE *result, int createflag)
{
    int rv;

    if (str && str[0] != LOCALNAMEPREFIX) {
	rv = (int) AGDISC(g, id)->map(AGCLOS(g, id), objtype, str, result,
				createflag);
	if (rv)
	    return rv;
    }

    /* either an internal ID, or disc. can't map strings */
    if (str) {
	rv = aginternalmaplookup(g, objtype, str, result);
	if (rv)
	    return rv;
    } else
	rv = 0;

    if (createflag) {
	/* get a new anonymous ID, and store in the internal map */
	rv = (int) AGDISC(g, id)->map(AGCLOS(g, id), objtype, NULL, result,
				createflag);
	if (rv && str)
	    aginternalmapinsert(g, objtype, str, *result);
    }
    return rv;
}

int agallocid(Agraph_t * g, int objtype, IDTYPE request)
{
    return (int) AGDISC(g, id)->alloc(AGCLOS(g, id), objtype, request);
}

void agfreeid(Agraph_t * g, int objtype, IDTYPE id)
{
    (void) aginternalmapdelete(g, objtype, id);
    (AGDISC(g, id)->free) (AGCLOS(g, id), objtype, id);
}

/* agnameof:
 * Return string representation of object.
 * In general, returns the name of node or graph,
 * and the key of an edge. If edge is anonymous, returns NULL.
 * Uses static buffer for anonymous graphs.
 */
char *agnameof(void *obj)
{
    Agraph_t *g;
    char *rv;

    /* perform internal lookup first */
    g = agraphof(obj);
    rv = aginternalmapprint(g, AGTYPE(obj), AGID(obj));
    if (rv != NULL)
	return rv;

    if (AGDISC(g, id)->print) {
	rv = AGDISC(g, id)->print(AGCLOS(g, id), AGTYPE(obj), AGID(obj));
	if (rv != NULL)
	    return rv;
    }
    if (AGTYPE(obj) != AGEDGE) {
	static char buf[32];
	snprintf(buf, sizeof(buf), "%c%" PRIu64, LOCALNAMEPREFIX, AGID(obj));
	rv = buf;
    }
    else
	rv = 0;
    return rv;
}

/* register a graph object in an external namespace */
void agregister(Agraph_t * g, int objtype, void *obj)
{
	AGDISC(g, id)->idregister(AGCLOS(g, id), objtype, obj);
}
