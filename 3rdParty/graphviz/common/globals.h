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

#ifdef GVDLL
#ifdef GVC_EXPORTS
#define GLOBALS_API
#else
#define GLOBALS_API
#endif
#endif

#ifndef GLOBALS_API
#define GLOBALS_API /* nothing */
#endif

#ifndef EXTERN
#define EXTERN extern
#endif

    GLOBALS_API EXTERN char *Version;
    GLOBALS_API EXTERN char **Files;    /* from command line */
    GLOBALS_API EXTERN const char **Lib;        /* from command line */
    GLOBALS_API EXTERN char *CmdName;
    GLOBALS_API EXTERN char *Gvimagepath; /* Per-graph path of files allowed in image attributes  (also ps libs) */

    GLOBALS_API EXTERN unsigned char Verbose;
    GLOBALS_API EXTERN unsigned char Reduce;
    GLOBALS_API EXTERN int MemTest;
    GLOBALS_API EXTERN char *HTTPServerEnVar;
    GLOBALS_API EXTERN int graphviz_errors;
    GLOBALS_API EXTERN int Nop;
    GLOBALS_API EXTERN double PSinputscale;
    GLOBALS_API EXTERN int Show_cnt;
    GLOBALS_API EXTERN char** Show_boxes;    /* emit code for correct box coordinates */
    GLOBALS_API EXTERN int CL_type;        /* NONE, LOCAL, GLOBAL */
    GLOBALS_API EXTERN unsigned char Concentrate;    /* if parallel edges should be merged */
    GLOBALS_API EXTERN double Epsilon;    /* defined in input_graph */
    GLOBALS_API EXTERN int MaxIter;
    GLOBALS_API EXTERN int Ndim;
    GLOBALS_API EXTERN int State;        /* last finished phase */
    GLOBALS_API EXTERN int EdgeLabelsDone;    /* true if edge labels have been positioned */
    GLOBALS_API EXTERN double Initial_dist;
    GLOBALS_API EXTERN double Damping;
    GLOBALS_API EXTERN int Y_invert;    /* invert y in dot & plain output */
    GLOBALS_API EXTERN int GvExitOnUsage;   /* gvParseArgs() should exit on usage or error */

    GLOBALS_API EXTERN Agsym_t
    *G_activepencolor, *G_activefillcolor,
    *G_visitedpencolor, *G_visitedfillcolor,
    *G_deletedpencolor, *G_deletedfillcolor,
    *G_ordering, *G_peripheries, *G_penwidth,
    *G_gradientangle, *G_margin;
    GLOBALS_API EXTERN Agsym_t
    *N_height, *N_width, *N_shape, *N_color, *N_fillcolor,
    *N_activepencolor, *N_activefillcolor,
    *N_selectedpencolor, *N_selectedfillcolor,
    *N_visitedpencolor, *N_visitedfillcolor,
    *N_deletedpencolor, *N_deletedfillcolor,
    *N_fontsize, *N_fontname, *N_fontcolor, *N_margin,
    *N_label, *N_xlabel, *N_nojustify, *N_style, *N_showboxes,
    *N_sides, *N_peripheries, *N_ordering, *N_orientation,
    *N_skew, *N_distortion, *N_fixed, *N_imagescale, *N_imagepos, *N_layer,
    *N_group, *N_comment, *N_vertices, *N_z,
    *N_penwidth, *N_gradientangle;
    GLOBALS_API EXTERN Agsym_t
    *E_weight, *E_minlen, *E_color, *E_fillcolor,
    *E_activepencolor, *E_activefillcolor,
    *E_selectedpencolor, *E_selectedfillcolor,
    *E_visitedpencolor, *E_visitedfillcolor,
    *E_deletedpencolor, *E_deletedfillcolor,
    *E_fontsize, *E_fontname, *E_fontcolor,
    *E_label, *E_xlabel, *E_dir, *E_style, *E_decorate,
    *E_showboxes, *E_arrowsz, *E_constr, *E_layer,
    *E_comment, *E_label_float,
    *E_samehead, *E_sametail,
    *E_arrowhead, *E_arrowtail,
    *E_headlabel, *E_taillabel,
    *E_labelfontsize, *E_labelfontname, *E_labelfontcolor,
    *E_labeldistance, *E_labelangle,
    *E_tailclip, *E_headclip,
    *E_penwidth;

    GLOBALS_API extern struct fdpParms_s* fdp_parms;

#undef EXTERN
#undef GLOBALS_API
