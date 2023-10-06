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

#include "types.h"
#include "macros.h"
#include "const.h"
#include "globals.h"
#include "memory.h"
#include "colorprocs.h"		/* must follow color.h (in types.h) */
#include "geomprocs.h"		/* must follow geom.h (in types.h) */
#include "../cgraph/agxbuf.h"
#include "utils.h"		/* must follow types.h and agxbuf.h */
#include "../gvc/gvplugin.h"		/* must follow gvcext.h (in types.h) */
#include "../gvc/gvcjob.h"		/* must follow gvcext.h (in types.h) */
#include "../gvc/gvcint.h"		/* must follow gvcext.h (in types.h) */
#include "../gvc/gvcproc.h"		/* must follow gvcext.h (in types.h) */

    typedef struct epsf_s {
	int macro_id;
	point offset;
    } epsf_t;

#ifdef GVDLL
#ifdef GVC_EXPORTS
#define RENDER_API
#else
#define RENDER_API
#endif
#endif

#ifndef RENDER_API
#define RENDER_API /* nothing */
#endif

	RENDER_API void add_box(path *, boxf);
    RENDER_API void arrow_flags(Agedge_t * e, int *sflag, int *eflag);
    RENDER_API boxf arrow_bb(pointf p, pointf u, double arrowsize);
    RENDER_API void arrow_gen(GVJ_t * job, emit_state_t emit_state, pointf p, pointf u,
			  double arrowsize, double penwidth, int flag);
    RENDER_API int arrowEndClip(edge_t*, pointf*, int, int , bezier*, int eflag);
    RENDER_API int arrowStartClip(edge_t*, pointf* ps, int, int, bezier*, int sflag);
    RENDER_API void arrowOrthoClip(edge_t*, pointf* ps, int, int, bezier*, int sflag, int eflag);
    RENDER_API void beginpath(path *, Agedge_t *, int, pathend_t *, bool);
    RENDER_API void bezier_clip(inside_t * inside_context,
			    bool(*insidefn) (inside_t * inside_context,
						pointf p), pointf * sp, bool left_inside);
    RENDER_API shape_desc *bind_shape(char *name, node_t *);
    RENDER_API void makeStraightEdge(graph_t * g, edge_t * e, int edgetype, splineInfo * info);
    RENDER_API void makeStraightEdges(graph_t* g, edge_t** edges, int e_cnt, int et, splineInfo* sinfo);
    RENDER_API void clip_and_install(edge_t * fe, node_t * hn,
	pointf * ps, int pn, splineInfo * info);
    RENDER_API char* charsetToStr (int c);
    RENDER_API pointf coord(node_t * n);
    RENDER_API void do_graph_label(graph_t * sg);
    RENDER_API void graph_init(graph_t * g, bool use_rankdir);
    RENDER_API void graph_cleanup(graph_t * g);
    RENDER_API int dotneato_args_initialize(GVC_t * gvc, int, char **);
    RENDER_API int dotneato_usage(int);
    RENDER_API void dotneato_postprocess(Agraph_t *);
    RENDER_API void gv_postprocess(Agraph_t *, int);
    RENDER_API Ppolyline_t* ellipticWedge (pointf ctr, double major, double minor, double angle0, double angle1);
    RENDER_API void emit_clusters(GVJ_t * job, Agraph_t * g, int flags);
    RENDER_API char* getObjId (GVJ_t* job, void* obj, agxbuf* xb);
    RENDER_API void emit_graph(GVJ_t * job, graph_t * g);
    RENDER_API void emit_label(GVJ_t * job, emit_state_t emit_state, textlabel_t *);
    RENDER_API int emit_once(char *message);
    RENDER_API void emit_map_rect(GVJ_t *job, boxf b);
    RENDER_API void endpath(path *, Agedge_t *, int, pathend_t *, bool);
    RENDER_API void epsf_init(node_t * n);
    RENDER_API void epsf_free(node_t * n);
    RENDER_API shape_desc *find_user_shape(const char *);
    RENDER_API void free_label(textlabel_t *);
    RENDER_API void free_textspan(textspan_t * tl, int);
    RENDER_API void getdouble(graph_t * g, char *name, double *result);
    RENDER_API splines *getsplinepoints(edge_t * e);
    RENDER_API void gv_fixLocale (int set);
    RENDER_API void gv_free_splines(edge_t * e);
    RENDER_API void gv_cleanup_edge(Agedge_t * e);
    RENDER_API void gv_cleanup_node(Agnode_t * n);
    RENDER_API void* init_xdot (Agraph_t* g);
    RENDER_API int initMapData (GVJ_t*, char*, char*, char*, char*, char*, void*);
    RENDER_API bool isPolygon(node_t *);
    RENDER_API void makeSelfEdge(edge_t * edges[], int ind, int cnt,
	double sizex, double sizey, splineInfo * sinfo);
    RENDER_API textlabel_t *make_label(void *obj, char *str, int kind, double fontsize, char *fontname, char *fontcolor);
    RENDER_API bezier *new_spline(edge_t * e, int sz);
    RENDER_API char **parse_style(char *s);
    RENDER_API void place_graph_label(Agraph_t *);
    RENDER_API int place_portlabel(edge_t * e, bool head_p);
    RENDER_API void makePortLabels(edge_t * e);
    RENDER_API pointf edgeMidpoint(graph_t* g, edge_t * e);
    RENDER_API void addEdgeLabels(edge_t *e);
    RENDER_API void pop_obj_state(GVJ_t *job);
    RENDER_API obj_state_t* push_obj_state(GVJ_t *job);
    RENDER_API int rank(graph_t * g, int balance, int maxiter);
    RENDER_API int rank2(graph_t * g, int balance, int maxiter, int search_size);
    RENDER_API port resolvePort(node_t*  n, node_t* other, port* oldport);
    RENDER_API void resolvePorts (edge_t* e);
    RENDER_API void round_corners(GVJ_t * job, pointf * AF, int sides, int style, int filled);
    RENDER_API int routesplinesinit(void);
    RENDER_API pointf *routesplines(path *, int *);
    RENDER_API void routesplinesterm(void);
    RENDER_API pointf* simpleSplineRoute (pointf, pointf, Ppoly_t, int*, int);
    RENDER_API pointf *routepolylines(path* pp, int* npoints);
    RENDER_API double selfRightSpace(edge_t *e);
    RENDER_API shape_kind shapeOf(node_t *);
    RENDER_API void shape_clip(node_t * n, pointf curve[4]);
    RENDER_API void make_simple_label (GVC_t * gvc, textlabel_t* rv);
    RENDER_API int stripedBox (GVJ_t * job, pointf* AF, char* clrs, int rotate);
    RENDER_API stroke_t taper (bezier*, double (*radfunc_t)(double,double,double), double initwid, int linejoin, int linecap);
    RENDER_API pointf textspan_size(GVC_t * gvc, textspan_t * span);
    RENDER_API Dt_t * textfont_dict_open(GVC_t *gvc);
    RENDER_API void textfont_dict_close(GVC_t *gvc);
    RENDER_API void translate_bb(Agraph_t *, int);
    RENDER_API int wedgedEllipse (GVJ_t* job, pointf * pf, char* clrs);
    RENDER_API void update_bb_bz(boxf *bb, pointf *cp);
    RENDER_API boxf xdotBB (graph_t* g);

#undef RENDER_API

