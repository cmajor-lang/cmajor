/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "render.h"
#include "../cgraph/agxbuf.h"
#include "../gvc/gvc.h"

#define YDIR(y) (Y_invert ? (Y_off - (y)) : (y))
#define YFDIR(y) (Y_invert ? (YF_off - (y)) : (y))

static double Y_off;        /* ymin + ymax */
static double YF_off;       /* Y_off in inches */

double yDir (double y)
{
    return YDIR(y);
}

static int (*putstr) (void *chan, const char *str);

static void agputs (const char* s, FILE* fp)
{
    putstr(fp, s);
}

static void agputc(char c, FILE *fp) {
    static char buf[2] = {'\0','\0'};
    buf[0] = c;
    putstr(fp, buf);
}

static void printstring(FILE * f, const char *prefix, const char *s)
{
    if (prefix) agputs(prefix, f);
    agputs(s, f);
}

static void printint(FILE * f, const char *prefix, int i)
{
    char buf[BUFSIZ];

    if (prefix) agputs(prefix, f);
    snprintf(buf, sizeof(buf), "%d", i);
    agputs(buf, f);
}

static void printdouble(FILE * f, const char *prefix, double v)
{
    char buf[BUFSIZ];

    if (prefix) agputs(prefix, f);
    snprintf(buf, sizeof(buf), "%.5g", v);
    agputs(buf, f);
}

static void printpoint(FILE * f, pointf p)
{
    printdouble(f, (char*) " ", PS2INCH(p.x));
    printdouble(f, (char*) " ", PS2INCH(YDIR(p.y)));
}

/* setYInvert:
 * Set parameters used to flip coordinate system (y=0 at top).
 * Values do not need to be unset, since if Y_invert is set, it's
 * set for * all graphs during current run, so each will
 * reinitialize the values for its bbox.
 */
static void setYInvert(graph_t * g)
{
    if (Y_invert) {
	Y_off = GD_bb(g).UR.y + GD_bb(g).LL.y;
	YF_off = PS2INCH(Y_off);
    }
}

/* canon:
 * Canonicalize a string which may not have been allocated using agstrdup.
 */
static char* canon (graph_t *g, char* s)
{
    char* ns = agstrdup (g, s);
    char* cs = agcanonStr (ns);
    agstrfree (g, ns);
    return cs;
}

static void writenodeandport(FILE *f, node_t *node, char *portname) {
    char *name;
    if (IS_CLUST_NODE(node))
	name = canon (agraphof(node), strchr(agnameof(node), ':') + 1);
    else
	name = agcanonStr (agnameof(node));
    printstring(f, (char*) " ", name); /* slimey i know */
    if (portname && *portname)
	printstring(f, (char*) ":", agcanonStr(portname));
}

/* _write_plain:
 */
void write_plain(GVJ_t *job, graph_t *g, FILE *f, bool extend) {
    int i, j, splinePoints;
    char *tport, *hport;
    node_t *n;
    edge_t *e;
    bezier bz;
    pointf pt;
    char *lbl;
    char* fillcolor;

    putstr = g->clos->disc.io->putstr;
//    setup_graph(job, g);
    setYInvert(g);
    pt = GD_bb(g).UR;
    printdouble(f, (char*) "graph ", job->zoom);
    printdouble(f, (char*) " ", PS2INCH(pt.x));
    printdouble(f, (char*) " ", PS2INCH(pt.y));
    agputc('\n', f);
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (IS_CLUST_NODE(n))
	    continue;
	printstring(f, "node ", agcanonStr(agnameof(n)));
	printpoint(f, ND_coord(n));
	if (ND_label(n)->html)   /* if html, get original text */
	    lbl = agcanonStr (agxget(n, N_label));
	else
	    lbl = canon(agraphof(n),ND_label(n)->text);
        printdouble(f, (char*) " ", ND_width(n));
        printdouble(f, (char*) " ", ND_height(n));
        printstring(f, (char*) " ", lbl);
	printstring(f, (char*) " ", late_nnstring(n, N_style, (char*) "solid"));
	printstring(f, (char*) " ", ND_shape(n)->name);
	printstring(f, (char*) " ", late_nnstring(n, N_color, (char*) DEFAULT_COLOR));
	fillcolor = late_nnstring(n, N_fillcolor, (char*) "");
        if (fillcolor[0] == '\0')
	    fillcolor = late_nnstring(n, N_color, (char*) DEFAULT_FILL);
	printstring(f, (char*) " ", fillcolor);
	agputc('\n', f);
    }
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {

	    if (extend) {		//assuming these two attrs have already been created by cgraph
		if (!(tport = agget(e,"tailport")))
		    tport = (char*) "";
		if (!(hport = agget(e,"headport")))
		    hport = (char*) "";
	    }
	    else
		tport = hport = (char*) "";
	    if (ED_spl(e)) {
		splinePoints = 0;
		for (i = 0; i < ED_spl(e)->size; i++) {
		    bz = ED_spl(e)->list[i];
		    splinePoints += bz.size;
		}
		printstring(f, NULL, (char*) "edge");
		writenodeandport(f, agtail(e), tport);
		writenodeandport(f, aghead(e), hport);
		printint(f, (char*) " ", splinePoints);
		for (i = 0; i < ED_spl(e)->size; i++) {
		    bz = ED_spl(e)->list[i];
		    for (j = 0; j < bz.size; j++)
			printpoint(f, bz.list[j]);
		}
	    }
	    if (ED_label(e)) {
		printstring(f, (char*) " ", canon(agraphof(agtail(e)),ED_label(e)->text));
		printpoint(f, ED_label(e)->pos);
	    }
	    printstring(f, (char*) " ", late_nnstring(e, E_style, (char*) "solid"));
	    printstring(f, (char*) " ", late_nnstring(e, E_color, (char*) DEFAULT_COLOR));
	    agputc('\n', f);
	}
    }
    agputs("stop\n", f);
}

static void set_record_rects(node_t * n, field_t * f, agxbuf * xb)
{
    int i;

    if (f->n_flds == 0) {
	agxbprint(xb, "%.5g,%.5g,%.5g,%.5g ",
		f->b.LL.x + ND_coord(n).x,
		YDIR(f->b.LL.y + ND_coord(n).y),
		f->b.UR.x + ND_coord(n).x,
		YDIR(f->b.UR.y + ND_coord(n).y));
    }
    for (i = 0; i < f->n_flds; i++)
	set_record_rects(n, f->fld[i], xb);
}

static void rec_attach_bb(graph_t * g, Agsym_t* bbsym, Agsym_t* lpsym, Agsym_t* lwsym, Agsym_t* lhsym)
{
    int c;
    char buf[BUFSIZ];
    pointf pt;

    snprintf(buf, sizeof(buf), "%.5g,%.5g,%.5g,%.5g", GD_bb(g).LL.x,
             YDIR(GD_bb(g).LL.y), GD_bb(g).UR.x, YDIR(GD_bb(g).UR.y));
    agxset(g, bbsym, buf);
    if (GD_label(g) && GD_label(g)->text[0]) {
	pt = GD_label(g)->pos;
	snprintf(buf, sizeof(buf), "%.5g,%.5g", pt.x, YDIR(pt.y));
	agxset(g, lpsym, buf);
	pt = GD_label(g)->dimen;
	snprintf(buf, sizeof(buf), "%.2f", PS2INCH(pt.x));
	agxset (g, lwsym, buf);
	snprintf(buf, sizeof(buf), "%.2f", PS2INCH(pt.y));
	agxset (g, lhsym, buf);
    }
    for (c = 1; c <= GD_n_cluster(g); c++)
	rec_attach_bb(GD_clust(g)[c], bbsym, lpsym, lwsym, lhsym);
}

void attach_attrs_and_arrows(graph_t* g, int* sp, int* ep)
{
    int e_arrows;		/* graph has edges with end arrows */
    int s_arrows;		/* graph has edges with start arrows */
    int j, sides;
    char buf[BUFSIZ];		/* Used only for small strings */
    char xbuffer[BUFSIZ];	/* Initial buffer for xb */
    agxbuf xb;
    node_t *n;
    edge_t *e;
    pointf ptf;
    int dim3 = (GD_odim(g) >= 3);
    Agsym_t* bbsym = NULL;
    Agsym_t* lpsym = NULL;
    Agsym_t* lwsym = NULL;
    Agsym_t* lhsym = NULL;

    gv_fixLocale (1);
    e_arrows = s_arrows = 0;
    setYInvert(g);
    agxbinit(&xb, BUFSIZ, xbuffer);
    safe_dcl(g, AGNODE, (char*) "pos", (char*) "");
    safe_dcl(g, AGNODE, (char*) "rects", (char*) "");
    N_width = safe_dcl(g, AGNODE, (char*) "width", (char*) "");
    N_height = safe_dcl(g, AGNODE, (char*) "height", (char*) "");
    safe_dcl(g, AGEDGE, (char*) "pos", (char*) "");
    if (GD_has_labels(g) & NODE_XLABEL)
	safe_dcl(g, AGNODE, (char*) "xlp", (char*) "");
    if (GD_has_labels(g) & EDGE_LABEL)
	safe_dcl(g, AGEDGE, (char*) "lp", (char*) "");
    if (GD_has_labels(g) & EDGE_XLABEL)
	safe_dcl(g, AGEDGE, (char*) "xlp", (char*) "");
    if (GD_has_labels(g) & HEAD_LABEL)
	safe_dcl(g, AGEDGE, (char*) "head_lp", (char*) "");
    if (GD_has_labels(g) & TAIL_LABEL)
	safe_dcl(g, AGEDGE, (char*) "tail_lp", (char*) "");
    if (GD_has_labels(g) & GRAPH_LABEL) {
	lpsym = safe_dcl(g, AGRAPH, (char*) "lp", (char*) "");
	lwsym = safe_dcl(g, AGRAPH, (char*) "lwidth", (char*) "");
	lhsym = safe_dcl(g, AGRAPH, (char*) "lheight", (char*) "");
    }
    bbsym = safe_dcl(g, AGRAPH, (char*) "bb", (char*) "");
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (dim3) {
	    int k;

	    agxbprint(&xb, "%.5g,%.5g,%.5g", ND_coord(n).x, YDIR(ND_coord(n).y), POINTS_PER_INCH*(ND_pos(n)[2]));
	    for (k = 3; k < GD_odim(g); k++) {
		agxbprint(&xb, ",%.5g", POINTS_PER_INCH*(ND_pos(n)[k]));
	    }
	    agset(n, (char*) "pos", agxbuse(&xb));
	} else {
	    snprintf(buf, sizeof(buf), "%.5g,%.5g", ND_coord(n).x,
	             YDIR(ND_coord(n).y));
	    agset(n, (char*) "pos", buf);
	}
	snprintf(buf, sizeof(buf), "%.5g", PS2INCH(ND_ht(n)));
	agxset(n, N_height, buf);
	snprintf(buf, sizeof(buf), "%.5g", PS2INCH(ND_lw(n) + ND_rw(n)));
	agxset(n, N_width, buf);
	if (ND_xlabel(n) && ND_xlabel(n)->set) {
	    ptf = ND_xlabel(n)->pos;
	    snprintf(buf, sizeof(buf), "%.5g,%.5g", ptf.x, YDIR(ptf.y));
	    agset(n, (char*) "xlp", buf);
	}
	if (strcmp(ND_shape(n)->name, "record") == 0) {
	    set_record_rects(n, (field_t*)ND_shape_info(n), &xb);
	    agxbpop(&xb);	/* get rid of last space */
	    agset(n, (char*) "rects", agxbuse(&xb));
	} else {
	    polygon_t *poly;
	    if (N_vertices && isPolygon(n)) {
		poly = (polygon_t*)ND_shape_info(n);
		sides = poly->sides;
		if (sides < 3) {
		    char *p = agget(n, "samplepoints");
		    if (p)
			sides = atoi(p);
		    else
			sides = 8;
		    if (sides < 3)
			sides = 8;
		}
		for (int i = 0; i < sides; i++) {
		    if (i > 0)
			agxbputc(&xb, ' ');
		    if (poly->sides >= 3)
			agxbprint(&xb, "%.5g %.5g",
				PS2INCH(poly->vertices[i].x),
				YFDIR(PS2INCH(poly->vertices[i].y)));
		    else
			agxbprint(&xb, "%.5g %.5g",
				ND_width(n) / 2.0 * cos(i / (double) sides * M_PI * 2.0),
				YFDIR(ND_height(n) / 2.0 * sin(i / (double) sides * M_PI * 2.0)));
		}
		agxset(n, N_vertices, agxbuse(&xb));
	    }
	}
	if (State >= GVSPLINES) {
	    for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
		if (ED_edge_type(e) == IGNORED)
		    continue;
		if (ED_spl(e) == NULL)
		    continue;	/* reported in postproc */
		for (int i = 0; i < ED_spl(e)->size; i++) {
		    if (i > 0)
			agxbputc(&xb, ';');
		    if (ED_spl(e)->list[i].sflag) {
			s_arrows = 1;
			agxbprint(&xb, "s,%.5g,%.5g ",
				ED_spl(e)->list[i].sp.x,
				YDIR(ED_spl(e)->list[i].sp.y));
		    }
		    if (ED_spl(e)->list[i].eflag) {
			e_arrows = 1;
			agxbprint(&xb, "e,%.5g,%.5g ",
				ED_spl(e)->list[i].ep.x,
				YDIR(ED_spl(e)->list[i].ep.y));
		    }
		    for (j = 0; j < ED_spl(e)->list[i].size; j++) {
			if (j > 0)
			    agxbputc(&xb, ' ');
			ptf = ED_spl(e)->list[i].list[j];
			agxbprint(&xb, "%.5g,%.5g", ptf.x, YDIR(ptf.y));
		    }
		}
		agset(e, (char*) "pos", agxbuse(&xb));
		if (ED_label(e)) {
		    ptf = ED_label(e)->pos;
		    snprintf(buf, sizeof(buf), "%.5g,%.5g", ptf.x, YDIR(ptf.y));
		    agset(e, (char*) "lp", buf);
		}
		if (ED_xlabel(e) && ED_xlabel(e)->set) {
		    ptf = ED_xlabel(e)->pos;
		    snprintf(buf, sizeof(buf), "%.5g,%.5g", ptf.x, YDIR(ptf.y));
		    agset(e, (char*) "xlp", buf);
		}
		if (ED_head_label(e)) {
		    ptf = ED_head_label(e)->pos;
		    snprintf(buf, sizeof(buf), "%.5g,%.5g", ptf.x, YDIR(ptf.y));
		    agset(e, (char*) "head_lp", buf);
		}
		if (ED_tail_label(e)) {
		    ptf = ED_tail_label(e)->pos;
		    snprintf(buf, sizeof(buf), "%.5g,%.5g", ptf.x, YDIR(ptf.y));
		    agset(e, (char*) "tail_lp", buf);
		}
	    }
	}
    }
    rec_attach_bb(g, bbsym, lpsym, lwsym, lhsym);
    agxbfree(&xb);

    if (HAS_CLUST_EDGE(g))
	undoClusterEdges(g);

    *sp = s_arrows;
    *ep = e_arrows;
    gv_fixLocale (0);
}

void attach_attrs(graph_t * g)
{
    int e, s;
    attach_attrs_and_arrows (g, &s, &e);
}

