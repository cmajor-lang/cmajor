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

#include "../gvc/config.h"

#ifdef GVDLL
#ifdef EXPORT_CGHDR
#define CGHDR_API
#else
#define CGHDR_API
#endif
#endif

#ifndef CGHDR_API
#define CGHDR_API /* nothing */
#endif

#include "cgraph.h"

#undef streq
static inline bool streq(const char *a, const char *b) {
  return strcmp(a, b) == 0;
}

#define    SUCCESS                0
#define FAILURE                -1
#define LOCALNAMEPREFIX        '%'

#define AGDISC(g,d)            ((g)->clos->disc.d)
#define AGCLOS(g,d)            ((g)->clos->state.d)
#define AGNEW(g,t)            ((t*)(agalloc(g,sizeof(t))))

    /* functional definitions */
typedef Agobj_t *(*agobjsearchfn_t) (Agraph_t * g, Agobj_t * obj);
CGHDR_API int agapply(Agraph_t * g, Agobj_t * obj, agobjfn_t fn, void *arg,
        int preorder);

    /* global variables */
extern Agraph_t *Ag_G_global;
extern char *AgDataRecName;

    /* set ordering disciplines */
extern Dtdisc_t Ag_subnode_id_disc;
extern Dtdisc_t Ag_subnode_seq_disc;
extern Dtdisc_t Ag_mainedge_id_disc;
extern Dtdisc_t Ag_subedge_id_disc;
extern Dtdisc_t Ag_mainedge_seq_disc;
extern Dtdisc_t Ag_subedge_seq_disc;
extern Dtdisc_t Ag_subgraph_id_disc;

    /* internal constructor of graphs and subgraphs */
Agraph_t *agopen1(Agraph_t * g);
int agstrclose(Agraph_t * g);

    /* ref string management */
void agmarkhtmlstr(char *s);

/// Mask of `Agtag_s.seq` width
enum { SEQ_MASK = (1 << (sizeof(unsigned) * 8 - 4)) - 1 };

    /* object set management */
Agnode_t *agfindnode_by_id(Agraph_t * g, IDTYPE id);
uint64_t agnextseq(Agraph_t * g, int objtype);

/* dict helper functions */
Dict_t *agdtopen(Agraph_t * g, Dtdisc_t * disc, Dtmethod_t * method);
void agdtdisc(Agraph_t * g, Dict_t * dict, Dtdisc_t * disc);
int agdtdelete(Agraph_t * g, Dict_t * dict, void *obj);
int agdtclose(Agraph_t * g, Dict_t * dict);
void *agdictobjmem(Dict_t * dict, void * p, size_t size,
           Dtdisc_t * disc);
void agdictobjfree(Dict_t * dict, void * p, Dtdisc_t * disc);

    /* name-value pair operations */
CGHDR_API Agdatadict_t *agdatadict(Agraph_t * g, int cflag);
CGHDR_API Agattr_t *agattrrec(void *obj);

void agraphattr_init(Agraph_t * g);
int agraphattr_delete(Agraph_t * g);
void agnodeattr_init(Agraph_t *g, Agnode_t * n);
void agnodeattr_delete(Agnode_t * n);
void agedgeattr_init(Agraph_t *g, Agedge_t * e);
void agedgeattr_delete(Agedge_t * e);

    /* parsing and lexing graph files */
int aagparse(void);
void aglexinit(Agdisc_t * disc, void *ifile);
int aaglex(void);
void aglexeof(void);
void aglexbad(void);

    /* ID management */
int agmapnametoid(Agraph_t * g, int objtype, char *str,
          IDTYPE *result, int createflag);
int agallocid(Agraph_t * g, int objtype, IDTYPE request);
void agfreeid(Agraph_t * g, int objtype, IDTYPE id);
char *agprintid(Agobj_t * obj);
int aginternalmaplookup(Agraph_t * g, int objtype, char *str,
            IDTYPE *result);
void aginternalmapinsert(Agraph_t * g, int objtype, char *str,
             IDTYPE result);
char *aginternalmapprint(Agraph_t * g, int objtype, IDTYPE id);
int aginternalmapdelete(Agraph_t * g, int objtype, IDTYPE id);
void aginternalmapclose(Agraph_t * g);
void agregister(Agraph_t * g, int objtype, void *obj);

    /* internal set operations */
void agedgesetop(Agraph_t * g, Agedge_t * e, int insertion);
void agdelnodeimage(Agraph_t * g, Agnode_t * node, void *ignored);
void agdeledgeimage(Agraph_t * g, Agedge_t * edge, void *ignored);
CGHDR_API int agrename(Agobj_t * obj, char *newname);
void agrecclose(Agobj_t * obj);

void agmethod_init(Agraph_t * g, void *obj);
void agmethod_upd(Agraph_t * g, void *obj, Agsym_t * sym);
void agmethod_delete(Agraph_t * g, void *obj);

typedef enum { CB_INITIALIZE, CB_UPDATE, CB_DELETION, } cb_t;
void agrecord_callback(Agraph_t * g, Agobj_t * obj, cb_t kind,
               Agsym_t * optsym);
void aginitcb(Agraph_t * g, void *obj, Agcbstack_t * disc);
void agupdcb(Agraph_t * g, void *obj, Agsym_t * sym, Agcbstack_t * disc);
void agdelcb(Agraph_t * g, void *obj, Agcbstack_t * disc);
