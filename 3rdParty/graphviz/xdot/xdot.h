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
#ifdef EXPORT_XDOT
#define XDOT_API
#else
#define XDOT_API
#endif
#endif

#ifndef XDOT_API
#define XDOT_API /* nothing */
#endif

#define INITIAL_XDOT_CAPACITY 512

typedef enum {
    xd_none,
    xd_linear,
    xd_radial
} xdot_grad_type;

typedef struct {
    float frac;
    char* color;
} xdot_color_stop;

typedef struct {
    double x0, y0;
    double x1, y1;
    int n_stops;
    xdot_color_stop* stops;
} xdot_linear_grad;

typedef struct {
    double x0, y0, r0;
    double x1, y1, r1;
    int n_stops;
    xdot_color_stop* stops;
} xdot_radial_grad;

typedef struct {
    xdot_grad_type type;
    union {
    char* clr;
    xdot_linear_grad ling;
    xdot_radial_grad ring;
    } u;
} xdot_color;

typedef enum {
    xd_left, xd_center, xd_right
} xdot_align;

typedef struct {
    double x, y, z;
} xdot_point;

typedef struct {
    double x, y, w, h;
} xdot_rect;

typedef struct {
    size_t cnt;
    xdot_point* pts;
} xdot_polyline;

typedef struct {
  double x, y;
  xdot_align align;
  double width;
  char* text;
} xdot_text;

typedef struct {
    xdot_rect pos;
    char* name;
} xdot_image;

typedef struct {
    double size;
    char* name;
} xdot_font;

typedef enum {
    xd_filled_ellipse, xd_unfilled_ellipse,
    xd_filled_polygon, xd_unfilled_polygon,
    xd_filled_bezier,  xd_unfilled_bezier,
    xd_polyline,       xd_text,
    xd_fill_color,     xd_pen_color, xd_font, xd_style, xd_image,
    xd_grad_fill_color,     xd_grad_pen_color,
    xd_fontchar
} xdot_kind;

typedef enum {
    xop_ellipse,
    xop_polygon,
    xop_bezier,
    xop_polyline,       xop_text,
    xop_fill_color,     xop_pen_color, xop_font, xop_style, xop_image,
    xop_grad_color,
    xop_fontchar
} xop_kind;

typedef struct _xdot_op xdot_op;
typedef void (*drawfunc_t)(xdot_op*, int);
typedef void (*freefunc_t)(xdot_op*);

struct _xdot_op {
    xdot_kind kind;
    union {
      xdot_rect ellipse;       /* xd_filled_ellipse, xd_unfilled_ellipse */
      xdot_polyline polygon;   /* xd_filled_polygon, xd_unfilled_polygon */
      xdot_polyline polyline;  /* xd_polyline */
      xdot_polyline bezier;    /* xd_filled_bezier,  xd_unfilled_bezier */
      xdot_text text;          /* xd_text */
      xdot_image image;        /* xd_image */
      char* color;             /* xd_fill_color, xd_pen_color */
      xdot_color grad_color;   /* xd_grad_fill_color, xd_grad_pen_color */
      xdot_font font;          /* xd_font */
      char* style;             /* xd_style */
      unsigned int fontchar;   /* xd_fontchar */
    } u;
    drawfunc_t drawfunc;
};

#define XDOT_PARSE_ERROR 1

typedef struct {
    size_t cnt;  /* no. of xdot ops */
    size_t sz;   /* sizeof structure containing xdot_op as first field */
    xdot_op* ops;
    freefunc_t freefunc;
    int flags;
} xdot;

typedef struct {
    size_t cnt;  /* no. of xdot ops */
    size_t n_ellipse;
    size_t n_polygon;
    size_t n_polygon_pts;
    size_t n_polyline;
    size_t n_polyline_pts;
    size_t n_bezier;
    size_t n_bezier_pts;
    size_t n_text;
    size_t n_font;
    size_t n_style;
    size_t n_color;
    size_t n_image;
    size_t n_gradcolor;
    size_t n_fontchar;
} xdot_stats;

/* ops are indexed by xop_kind */
XDOT_API xdot *parseXDotF(char*, drawfunc_t opfns[], size_t sz);
XDOT_API xdot *parseXDotFOn(char*, drawfunc_t opfns[], size_t sz, xdot*);
XDOT_API xdot* parseXDot (char*);
XDOT_API char* sprintXDot (xdot*);
XDOT_API void fprintXDot (FILE*, xdot*);
XDOT_API void jsonXDot (FILE*, xdot*);
XDOT_API void freeXDot (xdot*);
XDOT_API int statXDot (xdot*, xdot_stats*);
XDOT_API xdot_grad_type colorTypeXDot (char*);
XDOT_API char* parseXDotColor (char* cp, xdot_color* clr);
XDOT_API void freeXDotColor (xdot_color*);
