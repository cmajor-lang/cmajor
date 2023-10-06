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

#include "../cgraph/agxbuf.h"

#ifdef GVDLL
#ifdef GVC_EXPORTS
#define UTILS_API
#else
#define UTILS_API
#endif
#endif

#ifndef UTILS_API
#define UTILS_API /* nothing */
#endif

// options to tweak the behavior of XML escaping
typedef struct {
  // assume no embedded escapes, and escape "\n" and "\r"
  unsigned raw : 1;
  // escape '-'
  unsigned dash : 1;
  // escape consecutive ' '
  unsigned nbsp : 1;
  // anticipate non-ASCII characters that need to be encoded
  unsigned utf8 : 1;
} xml_flags_t;

UTILS_API nodequeue *new_queue(int);
UTILS_API void free_queue(nodequeue *);
UTILS_API void enqueue(nodequeue *, Agnode_t *);
UTILS_API Agnode_t *dequeue(nodequeue *);
UTILS_API pointf Bezier(pointf *, int, double, pointf *, pointf *);
UTILS_API void attach_attrs_and_arrows(graph_t *, int *, int *);

/** XML-escape a string
 *
 * \param s Source string to process.
 * \param flags Options of how to configure escaping.
 * \param cb An `fputs` analogue for emitting escaped output.
 * \param state Caller-defined data to pass to `cb`.
 * \return The first negative value `cb` returns or the last return value of
 *   `cb`.
 */
UTILS_API int xml_escape(const char *s, xml_flags_t flags,
                         int (*cb)(void *state, const char *s), void *state);

UTILS_API void write_plain(GVJ_t *job, graph_t *g, FILE *f, bool extend);
UTILS_API double yDir(double y);
UTILS_API char *ps_string(char *s, int);
UTILS_API char *strdup_and_subst_obj(char *str, void *obj);
UTILS_API void epsf_emit_body(GVJ_t *job, usershape_t *us);
UTILS_API void epsf_define(GVJ_t *job);
UTILS_API void undoClusterEdges(graph_t *g);
UTILS_API Dt_t *mkClustMap(Agraph_t *g);
UTILS_API Agraph_t *findCluster(Dt_t *map, char *name);
UTILS_API attrsym_t *safe_dcl(graph_t *g, int obj_kind, char *name,
                              char *defaultValue);

UTILS_API int late_int(void *obj, Agsym_t *attr, int defaultValue, int minimum);
UTILS_API double late_double(void *obj, Agsym_t *attr, double defaultValue,
                             double minimum);
UTILS_API char *late_nnstring(void *obj, Agsym_t *attr, char *defaultValue);
UTILS_API char *late_string(void *obj, Agsym_t *attr, char *defaultValue);
UTILS_API bool late_bool(void *obj, Agsym_t *attr, bool defaultValue);
UTILS_API double get_inputscale(graph_t *g);

UTILS_API Agnode_t *UF_find(Agnode_t *);
UTILS_API Agnode_t *UF_union(Agnode_t *, Agnode_t *);
UTILS_API void UF_singleton(Agnode_t *);
UTILS_API void UF_setname(Agnode_t *, Agnode_t *);

UTILS_API char *Fgets(FILE *fp);
UTILS_API const char *safefile(const char *filename);

UTILS_API bool mapBool(const char *p, bool defaultValue);
UTILS_API bool mapbool(const char *p);
UTILS_API int maptoken(char *, char **, int *);

UTILS_API bool findStopColor(char *colorlist, char *clrs[2], float *frac);
UTILS_API int test_toggle(void);

UTILS_API void common_init_node(node_t *n);
UTILS_API int common_init_edge(edge_t *e);

UTILS_API void updateBB(graph_t *g, textlabel_t *lp);
UTILS_API void compute_bb(Agraph_t *);
UTILS_API boxf polyBB(polygon_t *poly);
UTILS_API bool overlap_node(node_t *n, boxf b);
UTILS_API bool overlap_label(textlabel_t *lp, boxf b);
UTILS_API bool overlap_edge(edge_t *e, boxf b);

UTILS_API void get_gradient_points(pointf *A, pointf *G, int n, double angle,
                                   int flags);

UTILS_API void processClusterEdges(graph_t *g);

UTILS_API char *latin1ToUTF8(char *);
UTILS_API char *htmlEntityUTF8(char *, graph_t *g);
UTILS_API char *utf8ToLatin1(char *ins);
UTILS_API char *scanEntity(char *t, agxbuf *xb);

UTILS_API pointf dotneato_closest(splines *spl, pointf p);
UTILS_API pointf spline_at_y(splines *spl, double y);

UTILS_API Agsym_t *setAttr(graph_t *, void *, char *name, char *value,
                           Agsym_t *);
UTILS_API void setEdgeType(graph_t *g, int defaultValue);
UTILS_API bool is_a_cluster(Agraph_t *g);

/* from postproc.c */
UTILS_API void gv_nodesize(Agnode_t *n, bool flip);

#ifndef HAVE_DRAND48
UTILS_API double drand48(void);
#endif

/* from timing.c */
UTILS_API void start_timer(void);
UTILS_API double elapsed_sec(void);

/* from psusershape.c */
UTILS_API void cat_libfile(GVJ_t *job, const char **arglib,
                           const char **stdlib);

// variant of `isalpha` that assumes a C locale
static inline bool isalpha_no_locale(char c) {
  if (c >= 'a' && c <= 'z')
    return true;
  if (c >= 'A' && c <= 'Z')
    return true;
  return false;
}

#undef UTILS_API
