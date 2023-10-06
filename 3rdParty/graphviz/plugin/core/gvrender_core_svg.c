/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

/* Comments on the SVG coordinate system (SN 8 Dec 2006):
   The initial <svg> element defines the SVG coordinate system so
   that the graphviz canvas (in units of points) fits the intended
   absolute size in inches.  After this, the situation should be
   that "px" = "pt" in SVG, so we can dispense with stating units.
   Also, the input units (such as fontsize) should be preserved
   without scaling in the output SVG (as long as the graph size
   was not constrained.)
 */

#include "../../gvc/config.h"

#include "../../common/macros.h"
#include "../../common/const.h"

#include "../../gvc/gvplugin_render.h"
#include "../../cgraph/agxbuf.h"
#include "../../cgraph/unreachable.h"
#include "../../common/utils.h"
#include "../../gvc/gvplugin_device.h"
#include "../../gvc/gvio.h"
#include "../../gvc/gvcint.h"
#include "../../cgraph/strcasecmp.h"

#define LOCALNAMEPREFIX		'%'

#ifndef EDGEALIGN
  #define EDGEALIGN 0
#endif

typedef enum { FORMAT_SVG, FORMAT_SVGZ, } format_type;

/* SVG dash array */
static const char sdasharray[] = "5,2";
/* SVG dot array */
static const char sdotarray[] = "1,5";

static const char transparent[] = "transparent";
static const char none[] = "none";
static const char black[] = "black";

static void svg_bzptarray(GVJ_t * job, pointf * A, int n)
{
    int i;
    char c;

    c = 'M';			/* first point */
#if EDGEALIGN
    if (A[0].x <= A[n-1].x) {
#endif
	for (i = 0; i < n; i++) {
	    gvwrite(job, &c, 1);
            gvprintdouble(job, A[i].x);
            gvputc(job, ',');
            gvprintdouble(job, -A[i].y);
	    if (i == 0)
		c = 'C';		/* second point */
	    else
		c = ' ';		/* remaining points */
	}
#if EDGEALIGN
    } else {
	for (i = n-1; i >= 0; i--) {
	    gvwrite(job, &c, 1);
            gvprintdouble(job, A[i].x);
            gvputc(job, ',');
            gvprintdouble(job, -A[i].y);
	    if (i == 0)
		c = 'C';		/* second point */
	    else
		c = ' ';		/* remaining points */
	}
    }
#endif
}

static void svg_print_id_class(GVJ_t * job, char* id, char* idx, char* kind, void* obj)
{
    char* str;

    gvputs(job, "<g id=\"");
    gvputs_xml(job, id);
    if (idx) {
	gvputc(job, '_');
	gvputs_xml(job, idx);
    }
    gvprintf(job, "\" class=\"%s", kind);
    if ((str = agget(obj, "class")) && *str) {
	gvputc(job, ' ');
	gvputs_xml(job, str);
    }
    gvputc(job, '"');
}

/* svg_print_paint assumes the caller will set the opacity if the alpha channel
 * is greater than 0 and less than 255
 */
static void svg_print_paint(GVJ_t * job, gvcolor_t color)
{
    switch (color.type) {
    case COLOR_STRING:
	if (!strcmp(color.u.string, transparent))
	    gvputs(job, none);
	else
	    gvputs(job, color.u.string);
	break;
    case RGBA_BYTE:
	if (color.u.rgba[3] == 0)	/* transparent */
	    gvputs(job, none);
	else
	    gvprintf(job, "#%02x%02x%02x",
		     color.u.rgba[0], color.u.rgba[1], color.u.rgba[2]);
	break;
    default:
	UNREACHABLE(); // internal error
    }
}

/* svg_print_gradient_color assumes the caller will set the opacity if the
 * alpha channel is less than 255.
 *
 * "transparent" in SVG 2 gradients is considered to be black with 0 opacity,
 * so for compatibility with SVG 1.1 output we use black when the color string
 * is transparent and assume the caller will also check and set opacity 0.
 */
static void svg_print_gradient_color(GVJ_t * job, gvcolor_t color)
{
    switch (color.type) {
    case COLOR_STRING:
	if (!strcmp(color.u.string, transparent))
	    gvputs(job, black);
	else
	    gvputs(job, color.u.string);
	break;
    case RGBA_BYTE:
	gvprintf(job, "#%02x%02x%02x",
		 color.u.rgba[0], color.u.rgba[1], color.u.rgba[2]);
	break;
    default:
	UNREACHABLE(); // internal error
    }
}

static void svg_grstyle(GVJ_t * job, int filled, int gid)
{
    obj_state_t *obj = job->obj;

    gvputs(job, " fill=\"");
    if (filled == GRADIENT) {
	gvputs(job, "url(#");
	if (obj->id != NULL) {
	    gvputs_xml(job, obj->id);
	    gvputc(job, '_');
	}
	gvprintf(job, "l_%d)", gid);
    } else if (filled == RGRADIENT) {
	gvputs(job, "url(#");
	if (obj->id != NULL) {
	    gvputs_xml(job, obj->id);
	    gvputc(job, '_');
	}
	gvprintf(job, "r_%d)", gid);
    } else if (filled) {
	svg_print_paint(job, obj->fillcolor);
	if (obj->fillcolor.type == RGBA_BYTE
	    && obj->fillcolor.u.rgba[3] > 0
	    && obj->fillcolor.u.rgba[3] < 255)
	    gvprintf(job, "\" fill-opacity=\"%f",
		     (float)obj->fillcolor.u.rgba[3] / 255.0);
    } else {
	gvputs(job, "none");
    }
    gvputs(job, "\" stroke=\"");
    svg_print_paint(job, obj->pencolor);
    // will `gvprintdouble` output something different from `PENWIDTH_NORMAL`?
    const double GVPRINT_DOUBLE_THRESHOLD = 0.005;
    if (!(fabs(obj->penwidth - PENWIDTH_NORMAL) < GVPRINT_DOUBLE_THRESHOLD)) {
	gvputs(job, "\" stroke-width=\"");
        gvprintdouble(job, obj->penwidth);
    }
    if (obj->pen == PEN_DASHED) {
	gvprintf(job, "\" stroke-dasharray=\"%s", sdasharray);
    } else if (obj->pen == PEN_DOTTED) {
	gvprintf(job, "\" stroke-dasharray=\"%s", sdotarray);
    }
    if (obj->pencolor.type == RGBA_BYTE && obj->pencolor.u.rgba[3] > 0
	&& obj->pencolor.u.rgba[3] < 255)
	gvprintf(job, "\" stroke-opacity=\"%f",
		 (float)obj->pencolor.u.rgba[3] / 255.0);

    gvputc(job, '"');
}

static void svg_comment(GVJ_t * job, char *str)
{
    gvputs(job, "<!-- ");
    gvputs_xml(job, str);
    gvputs(job, " -->\n");
}

static void svg_begin_job(GVJ_t * job)
{
    char *s;
    gvputs(job,
	   "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n");
    if ((s = agget(job->gvc->g, "stylesheet")) && s[0]) {
	gvputs(job, "<?xml-stylesheet href=\"");
	gvputs(job, s);
	gvputs(job, "\" type=\"text/css\"?>\n");
    }
    gvputs(job, "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\"\n"
                " \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n"

                "<!-- Generated by ");
    gvputs_xml(job, job->common->info[0]);
    gvputs(job, " version ");
    gvputs_xml(job, job->common->info[1]);
    gvputs(job, " -->\n");
}

static void svg_begin_graph(GVJ_t * job)
{
    obj_state_t *obj = job->obj;

    gvputs(job, "<!--");
    if (agnameof(obj->u.g)[0] && agnameof(obj->u.g)[0] != LOCALNAMEPREFIX) {
	gvputs(job, " Title: ");
	gvputs_xml(job, agnameof(obj->u.g));
    }
    gvprintf(job, " Pages: %d -->\n",
	     job->pagesArraySize.x * job->pagesArraySize.y);

    gvprintf(job, "<svg width=\"%dpt\" height=\"%dpt\"\n",
	     job->width, job->height);
    gvprintf(job, " viewBox=\"%.2f %.2f %.2f %.2f\"",
	job->canvasBox.LL.x,
	job->canvasBox.LL.y,
	job->canvasBox.UR.x,
	job->canvasBox.UR.y);
    /* namespace of svg */
    gvputs(job, " xmlns=\"http://www.w3.org/2000/svg\""
    /* namespace of xlink */
                " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
                ">\n");
}

static void svg_end_graph(GVJ_t * job)
{
    gvputs(job, "</svg>\n");
}

static void svg_begin_layer(GVJ_t * job, char *layername, int layerNum,
			    int numLayers)
{
    (void)layerNum;
    (void)numLayers;

    obj_state_t *obj = job->obj;

    svg_print_id_class(job, layername, NULL, "layer", obj->u.g);
    gvputs(job, ">\n");
}

static void svg_end_layer(GVJ_t * job)
{
    gvputs(job, "</g>\n");
}

/* svg_begin_page:
 * Currently, svg output does not support pages.
 * FIX: If implemented, we must guarantee the id is unique.
 */
static void svg_begin_page(GVJ_t * job)
{
    obj_state_t *obj = job->obj;

    /* its really just a page of the graph, but its still a graph,
     * and it is the entire graph if we're not currently paging */
    svg_print_id_class(job, obj->id, NULL, "graph", obj->u.g);
    gvputs(job, " transform=\"scale(");
    // cannot be gvprintdouble because 2 digits precision insufficient
    gvprintf(job, "%g %g", job->scale.x, job->scale.y);
    gvprintf(job, ") rotate(%d) translate(", -job->rotation);
    gvprintdouble(job, job->translation.x);
    gvputc(job, ' ');
    gvprintdouble(job, -job->translation.y);
    gvputs(job, ")\">\n");
    /* default style */
    if (agnameof(obj->u.g)[0] && agnameof(obj->u.g)[0] != LOCALNAMEPREFIX) {
	gvputs(job, "<title>");
	gvputs_xml(job, agnameof(obj->u.g));
	gvputs(job, "</title>\n");
    }
}

static void svg_end_page(GVJ_t * job)
{
    gvputs(job, "</g>\n");
}

static void svg_begin_cluster(GVJ_t * job)
{
    obj_state_t *obj = job->obj;

    svg_print_id_class(job, obj->id, NULL, "cluster", obj->u.sg);
    gvputs(job, ">\n"
                "<title>");
    gvputs_xml(job, agnameof(obj->u.g));
    gvputs(job, "</title>\n");
}

static void svg_end_cluster(GVJ_t * job)
{
    gvputs(job, "</g>\n");
}

static void svg_begin_node(GVJ_t * job)
{
    obj_state_t *obj = job->obj;
    char* idx;

    if (job->layerNum > 1)
	idx = job->gvc->layerIDs[job->layerNum];
    else
	idx = NULL;
    svg_print_id_class(job, obj->id, idx, "node", obj->u.n);
    gvputs(job, ">\n"
                "<title>");
    gvputs_xml(job, agnameof(obj->u.n));
    gvputs(job, "</title>\n");
}

static void svg_end_node(GVJ_t * job)
{
    gvputs(job, "</g>\n");
}

static void svg_begin_edge(GVJ_t * job)
{
    obj_state_t *obj = job->obj;
    char *ename;

    svg_print_id_class(job, obj->id, NULL, "edge", obj->u.e);
    gvputs(job, ">\n"

                "<title>");
    ename = strdup_and_subst_obj("\\E", obj->u.e);
    gvputs_xml(job, ename);
    free(ename);
    gvputs(job, "</title>\n");
}

static void svg_end_edge(GVJ_t * job)
{
    gvputs(job, "</g>\n");
}

static void
svg_begin_anchor(GVJ_t * job, char *href, char *tooltip, char *target,
		 char *id)
{
    gvputs(job, "<g");
    if (id) {
	gvputs(job, " id=\"a_");
        gvputs_xml(job, id);
        gvputc(job, '"');
    }
    gvputs(job, ">"

                "<a");
    if (href && href[0]) {
	gvputs(job, " xlink:href=\"");
	const xml_flags_t flags = {0};
	xml_escape(href, flags, (int(*)(void*, const char*))gvputs, job);
	gvputc(job, '"');
    }
    if (tooltip && tooltip[0]) {
	gvputs(job, " xlink:title=\"");
    xml_flags_t flags = {};
    flags.raw = 1; flags.dash = 1; flags.nbsp = 1;
	xml_escape(tooltip, flags, (int(*)(void*, const char*))gvputs, job);
	gvputc(job, '"');
    }
    if (target && target[0]) {
	gvputs(job, " target=\"");
	gvputs_xml(job, target);
	gvputc(job, '"');
    }
    gvputs(job, ">\n");
}

static void svg_end_anchor(GVJ_t * job)
{
    gvputs(job, "</a>\n"
                "</g>\n");
}

static void svg_textspan(GVJ_t * job, pointf p, textspan_t * span)
{
    obj_state_t *obj = job->obj;
    PostscriptAlias *pA;
    char *family = NULL, *weight = NULL, *stretch = NULL, *style = NULL;
    unsigned int flags;

    gvputs(job, "<text");
    switch (span->just) {
    case 'l':
	gvputs(job, " text-anchor=\"start\"");
	break;
    case 'r':
	gvputs(job, " text-anchor=\"end\"");
	break;
    default:
    case 'n':
	gvputs(job, " text-anchor=\"middle\"");
	break;
    }
    p.y += span->yoffset_centerline;
    if (!obj->labeledgealigned) {
	gvputs(job, " x=\"");
        gvprintdouble(job, p.x);
        gvputs(job, "\" y=\"");
        gvprintdouble(job, -p.y);
        gvputs(job, "\"");
    }
    pA = span->font->postscript_alias;
    if (pA) {
	switch (GD_fontnames(job->gvc->g)) {
	case PSFONTS:
	    family = pA->name;
	    weight = pA->weight;
	    style = pA->style;
	    break;
	case SVGFONTS:
	    family = pA->svg_font_family;
	    weight = pA->svg_font_weight;
	    style = pA->svg_font_style;
	    break;
	default:
	case NATIVEFONTS:
	    family = pA->family;
	    weight = pA->weight;
	    style = pA->style;
	    break;
	}
	stretch = pA->stretch;

	gvprintf(job, " font-family=\"%s", family);
	if (pA->svg_font_family)
	    gvprintf(job, ",%s", pA->svg_font_family);
	gvputc(job, '"');
	if (weight)
	    gvprintf(job, " font-weight=\"%s\"", weight);
	if (stretch)
	    gvprintf(job, " font-stretch=\"%s\"", stretch);
	if (style)
	    gvprintf(job, " font-style=\"%s\"", style);
    } else
	gvprintf(job, " font-family=\"%s\"", span->font->name);
    if (span->font && (flags = span->font->flags)) {
	if ((flags & HTML_BF) && !weight)
	    gvputs(job, " font-weight=\"bold\"");
	if ((flags & HTML_IF) && !style)
	    gvputs(job, " font-style=\"italic\"");
	if (flags & (HTML_UL|HTML_S|HTML_OL)) {
	    int comma = 0;
	    gvputs(job, " text-decoration=\"");
	    if ((flags & HTML_UL)) {
		gvputs(job, "underline");
		comma = 1;
	    }
	    if (flags & HTML_OL) {
		gvprintf(job, "%soverline", (comma?",":""));
		comma = 1;
	    }
	    if (flags & HTML_S)
		gvprintf(job, "%sline-through", (comma?",":""));
	    gvputc(job, '"');
	}
	if (flags & HTML_SUP)
	    gvputs(job, " baseline-shift=\"super\"");
	if (flags & HTML_SUB)
	    gvputs(job, " baseline-shift=\"sub\"");
    }

    gvprintf(job, " font-size=\"%.2f\"", span->font->size);
    switch (obj->pencolor.type) {
    case COLOR_STRING:
	if (strcasecmp(obj->pencolor.u.string, "black"))
	    gvprintf(job, " fill=\"%s\"", obj->pencolor.u.string);
	break;
    case RGBA_BYTE:
	gvprintf(job, " fill=\"#%02x%02x%02x\"",
		 obj->pencolor.u.rgba[0], obj->pencolor.u.rgba[1],
		 obj->pencolor.u.rgba[2]);
	if (obj->pencolor.u.rgba[3] < 255)
	    gvprintf(job, " fill-opacity=\"%f\"", (float)obj->pencolor.u.rgba[3] / 255.0);
	break;
    default:
	UNREACHABLE(); // internal error
    }
    gvputc(job, '>');
    if (obj->labeledgealigned) {
	gvputs(job, "<textPath xlink:href=\"#");
	gvputs_xml(job, obj->id);
	gvputs(job, "_p\" startOffset=\"50%\"><tspan x=\"0\" dy=\"");
        gvprintdouble(job, -p.y);
        gvputs(job, "\">");
    }
    xml_flags_t xml_flags = {};
    xml_flags.raw = 1; xml_flags.dash = 1; xml_flags.nbsp = 1;
    xml_escape(span->str, xml_flags, (int(*)(void*, const char*))gvputs, job);
    if (obj->labeledgealigned)
	gvputs(job, "</tspan></textPath>");
    gvputs(job, "</text>\n");
}

static void svg_print_stop(GVJ_t * job, double offset, gvcolor_t color)
{
    if (fabs(offset - 0.0) < 0.0005)
	gvputs(job, "<stop offset=\"0\" style=\"stop-color:");
    else if (fabs(offset - 1.0) < 0.0005)
	gvputs(job, "<stop offset=\"1\" style=\"stop-color:");
    else
	gvprintf(job, "<stop offset=\"%.03f\" style=\"stop-color:", offset);
    svg_print_gradient_color(job, color);
    gvputs(job, ";stop-opacity:");
    if (color.type == RGBA_BYTE && color.u.rgba[3] < 255)
	gvprintf(job, "%f", (float)color.u.rgba[3] / 255.0);
    else if (color.type == COLOR_STRING && !strcmp(color.u.string, transparent))
	gvputs(job, "0");
    else
	gvputs(job, "1.");
    gvputs(job, ";\"/>\n");
}

/* svg_gradstyle
 * Outputs the SVG statements that define the gradient pattern
 */
static int svg_gradstyle(GVJ_t * job, pointf * A, int n)
{
    pointf G[2];
    static int gradId;
    int id = gradId++;

    obj_state_t *obj = job->obj;
    double angle = obj->gradient_angle * M_PI / 180; //angle of gradient line
    G[0].x = G[0].y = G[1].x = G[1].y = 0.;
    get_gradient_points(A, G, n, angle, 0);	//get points on gradient line

    gvputs(job, "<defs>\n<linearGradient id=\"");
    if (obj->id != NULL) {
        gvputs_xml(job, obj->id);
        gvputc(job, '_');
    }
    gvprintf(job, "l_%d\" gradientUnits=\"userSpaceOnUse\" ", id);
    gvputs(job, "x1=\"");
    gvprintdouble(job, G[0].x);
    gvputs(job, "\" y1=\"");
    gvprintdouble(job, G[0].y);
    gvputs(job, "\" x2=\"");
    gvprintdouble(job, G[1].x);
    gvputs(job, "\" y2=\"");
    gvprintdouble(job, G[1].y);
    gvputs(job, "\" >\n");

    svg_print_stop(job, obj->gradient_frac > 0 ? obj->gradient_frac - 0.001 : 0.0, obj->fillcolor);
    svg_print_stop(job, obj->gradient_frac > 0 ? obj->gradient_frac : 1.0, obj->stopcolor);

    gvputs(job, "</linearGradient>\n</defs>\n");
    return id;
}

/* svg_rgradstyle
 * Outputs the SVG statements that define the radial gradient pattern
 */
static int svg_rgradstyle(GVJ_t * job)
{
    double ifx, ify;
    static int rgradId;
    int id = rgradId++;

    obj_state_t *obj = job->obj;
    if (obj->gradient_angle == 0) {
	ifx = ify = 50;
    } else {
	double angle = obj->gradient_angle * M_PI / 180;	//angle of gradient line
	ifx = round(50 * (1 + cos(angle)));
	ify = round(50 * (1 - sin(angle)));
    }
    gvputs(job, "<defs>\n<radialGradient id=\"");
    if (obj->id != NULL) {
	gvputs_xml(job, obj->id);
	gvputc(job, '_');
    }
    gvprintf(job, "r_%d\" cx=\"50%%\" cy=\"50%%\" r=\"75%%\" "
	     "fx=\"%.0f%%\" fy=\"%.0f%%\">\n",
	     id, ifx, ify);

    svg_print_stop(job, 0.0, obj->fillcolor);
    svg_print_stop(job, 1.0, obj->stopcolor);

    gvputs(job, "</radialGradient>\n</defs>\n");
    return id;
}


static void svg_ellipse(GVJ_t * job, pointf * A, int filled)
{
    int gid = 0;

    /* A[] contains 2 points: the center and corner. */
    if (filled == GRADIENT) {
	gid = svg_gradstyle(job, A, 2);
    } else if (filled == RGRADIENT) {
	gid = svg_rgradstyle(job);
    }
    gvputs(job, "<ellipse");
    svg_grstyle(job, filled, gid);
    gvputs(job, " cx=\"");
    gvprintdouble(job, A[0].x);
    gvputs(job, "\" cy=\"");
    gvprintdouble(job, -A[0].y);
    gvputs(job, "\" rx=\"");
    gvprintdouble(job, A[1].x - A[0].x);
    gvputs(job, "\" ry=\"");
    gvprintdouble(job, A[1].y - A[0].y);
    gvputs(job, "\"/>\n");
}

static void
svg_bezier(GVJ_t * job, pointf * A, int n, int arrow_at_start,
	   int arrow_at_end, int filled)
{
    (void)arrow_at_start;
    (void)arrow_at_end;

    int gid = 0;
    obj_state_t *obj = job->obj;

    if (filled == GRADIENT) {
	gid = svg_gradstyle(job, A, n);
    } else if (filled == RGRADIENT) {
	gid = svg_rgradstyle(job);
    }
    gvputs(job, "<path");
    if (obj->labeledgealigned) {
	gvputs(job, " id=\"");
	gvputs_xml(job, obj->id);
	gvputs(job, "_p\" ");
    }
    svg_grstyle(job, filled, gid);
    gvputs(job, " d=\"");
    svg_bzptarray(job, A, n);
    gvputs(job, "\"/>\n");
}

static void svg_polygon(GVJ_t * job, pointf * A, int n, int filled)
{
    int i, gid = 0;
    if (filled == GRADIENT) {
	gid = svg_gradstyle(job, A, n);
    } else if (filled == RGRADIENT) {
	gid = svg_rgradstyle(job);
    }
    gvputs(job, "<polygon");
    svg_grstyle(job, filled, gid);
    gvputs(job, " points=\"");
    for (i = 0; i < n; i++) {
        gvprintdouble(job, A[i].x);
        gvputc(job, ',');
        gvprintdouble(job, -A[i].y);
        gvputc(job, ' ');
    }
    /* repeat the first point because Adobe SVG is broken */
    gvprintdouble(job, A[0].x);
    gvputc(job, ',');
    gvprintdouble(job, -A[0].y);
    gvputs(job, "\"/>\n");
}

static void svg_polyline(GVJ_t * job, pointf * A, int n)
{
    int i;

    gvputs(job, "<polyline");
    svg_grstyle(job, 0, 0);
    gvputs(job, " points=\"");
    for (i = 0; i < n; i++) {
        gvprintdouble(job, A[i].x);
        gvputc(job, ',');
        gvprintdouble(job, -A[i].y);
        if (i != n - 1) {
            gvputc(job, ' ');
        }
    }
    gvputs(job, "\"/>\n");
}

/* color names from http://www.w3.org/TR/SVG/types.html */
/* NB.  List must be LANG_C sorted */
static char *svg_knowncolors[] = {
    "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure",
    "beige", "bisque", "black", "blanchedalmond", "blue",
    "blueviolet", "brown", "burlywood",
    "cadetblue", "chartreuse", "chocolate", "coral",
    "cornflowerblue", "cornsilk", "crimson", "cyan",
    "darkblue", "darkcyan", "darkgoldenrod", "darkgray",
    "darkgreen", "darkgrey", "darkkhaki", "darkmagenta",
    "darkolivegreen", "darkorange", "darkorchid", "darkred",
    "darksalmon", "darkseagreen", "darkslateblue", "darkslategray",
    "darkslategrey", "darkturquoise", "darkviolet", "deeppink",
    "deepskyblue", "dimgray", "dimgrey", "dodgerblue",
    "firebrick", "floralwhite", "forestgreen", "fuchsia",
    "gainsboro", "ghostwhite", "gold", "goldenrod", "gray",
    "green", "greenyellow", "grey",
    "honeydew", "hotpink", "indianred",
    "indigo", "ivory", "khaki",
    "lavender", "lavenderblush", "lawngreen", "lemonchiffon",
    "lightblue", "lightcoral", "lightcyan", "lightgoldenrodyellow",
    "lightgray", "lightgreen", "lightgrey", "lightpink",
    "lightsalmon", "lightseagreen", "lightskyblue",
    "lightslategray", "lightslategrey", "lightsteelblue",
    "lightyellow", "lime", "limegreen", "linen",
    "magenta", "maroon", "mediumaquamarine", "mediumblue",
    "mediumorchid", "mediumpurple", "mediumseagreen",
    "mediumslateblue", "mediumspringgreen", "mediumturquoise",
    "mediumvioletred", "midnightblue", "mintcream",
    "mistyrose", "moccasin",
    "navajowhite", "navy", "oldlace",
    "olive", "olivedrab", "orange", "orangered", "orchid",
    "palegoldenrod", "palegreen", "paleturquoise",
    "palevioletred", "papayawhip", "peachpuff", "peru", "pink",
    "plum", "powderblue", "purple",
    "red", "rosybrown", "royalblue",
    "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell",
    "sienna", "silver", "skyblue", "slateblue", "slategray",
    "slategrey", "snow", "springgreen", "steelblue",
    "tan", "teal", "thistle", "tomato", "transparent", "turquoise",
    "violet",
    "wheat", "white", "whitesmoke",
    "yellow", "yellowgreen"
};

gvrender_engine_t svg_engine = {
    svg_begin_job,
    0,				/* svg_end_job */
    svg_begin_graph,
    svg_end_graph,
    svg_begin_layer,
    svg_end_layer,
    svg_begin_page,
    svg_end_page,
    svg_begin_cluster,
    svg_end_cluster,
    0,				/* svg_begin_nodes */
    0,				/* svg_end_nodes */
    0,				/* svg_begin_edges */
    0,				/* svg_end_edges */
    svg_begin_node,
    svg_end_node,
    svg_begin_edge,
    svg_end_edge,
    svg_begin_anchor,
    svg_end_anchor,
    0,				/* svg_begin_anchor */
    0,				/* svg_end_anchor */
    svg_textspan,
    0,				/* svg_resolve_color */
    svg_ellipse,
    svg_polygon,
    svg_bezier,
    svg_polyline,
    svg_comment,
    0,				/* svg_library_shape */
};

gvrender_features_t render_features_svg = {
    GVRENDER_Y_GOES_DOWN | GVRENDER_DOES_TRANSFORM | GVRENDER_DOES_LABELS | GVRENDER_DOES_MAPS | GVRENDER_DOES_TARGETS | GVRENDER_DOES_TOOLTIPS,	/* flags */
    4.,				/* default pad - graph units */
    svg_knowncolors,		/* knowncolors */
    sizeof(svg_knowncolors) / sizeof(char *),	/* sizeof knowncolors */
    RGBA_BYTE,			/* color_type */
};

gvdevice_features_t device_features_svg = {
    GVDEVICE_DOES_TRUECOLOR|GVDEVICE_DOES_LAYERS,  /* flags */
    {0., 0.},			/* default margin - points */
    {0., 0.},			/* default page width, height - points */
    {72., 72.},			/* default dpi */
};

gvdevice_features_t device_features_svgz = {
    GVDEVICE_DOES_TRUECOLOR|GVDEVICE_DOES_LAYERS|GVDEVICE_BINARY_FORMAT|GVDEVICE_COMPRESSED_FORMAT, /* flags */
    {0., 0.},			/* default margin - points */
    {0., 0.},			/* default page width, height - points */
    {72., 72.},			/* default dpi */
};

gvplugin_installed_t gvrender_svg_types[] = {
    {FORMAT_SVG, "svg", 1, &svg_engine, &render_features_svg},
    {0, NULL, 0, NULL, NULL}
};

gvplugin_installed_t gvdevice_svg_types[] = {
    {FORMAT_SVG, "svg:svg", 1, NULL, &device_features_svg},
#if HAVE_LIBZ
    {FORMAT_SVGZ, "svgz:svg", 1, NULL, &device_features_svgz},
#endif
    {0, NULL, 0, NULL, NULL}
};
