/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "../gvc/config.h"
#include "../cgraph/agxbuf.h"
#include "../cgraph/alloc.h"
#include "render.h"
#include "../pathplan/pathplan.h"

static int nedges, nboxes; /* total no. of edges and boxes used in routing */

static int routeinit;
/* static data used across multiple edges */
static Ppoint_t *polypoints;  /* vertices of polygon defined by boxes */
static int polypointn;        /* size of polypoints[] */
static Pedge_t *edges;        /* polygon edges passed to Proutespline */
static int edgen;             /* size of edges[] */

static int checkpath(int, boxf*, path*);
static void printpath(path * pp);
#ifdef DEBUG
static void printboxes(int boxn, boxf* boxes)
{
    pointf ll, ur;
    int bi;
    int newcnt = Show_cnt + boxn;

    Show_boxes = ALLOC(newcnt+2,Show_boxes,char*);
    for (bi = 0; bi < boxn; bi++) {
	ll = boxes[bi].LL, ur = boxes[bi].UR;
	agxbuf buf = {0};
	agxbprint(&buf, "%.0f %.0f %.0f %.0f pathbox", ll.x, ll.y, ur.x, ur.y);
	Show_boxes[bi + 1 + Show_cnt] = agxbdisown(&buf);
    }
    Show_cnt = newcnt;
    Show_boxes[Show_cnt+1] = NULL;
}

#if DEBUG > 1
static void psprintpolypts(Ppoint_t * p, int sz)
{
    int i;

    fprintf(stderr, "%%!\n");
    fprintf(stderr, "%% constraint poly\n");
    fprintf(stderr, "newpath\n");
    for (i = 0; i < sz; i++)
	fprintf(stderr, "%f %f %s\n", p[i].x, p[i].y, i == 0 ? "moveto" : "lineto");
    fprintf(stderr, "closepath stroke\n");
}
static void psprintpoint(point p)
{
    fprintf(stderr, "gsave\n");
    fprintf(stderr,
	    "newpath %d %d moveto %d %d 2 0 360 arc closepath fill stroke\n",
	    p.x, p.y, p.x, p.y);
    fprintf(stderr, "/Times-Roman findfont 4 scalefont setfont\n");
    fprintf(stderr, "%d %d moveto (\\(%d,%d\\)) show\n", p.x + 5, p.y + 5,
	    p.x, p.y);
    fprintf(stderr, "grestore\n");
}
static void psprintpointf(pointf p)
{
    fprintf(stderr, "gsave\n");
    fprintf(stderr,
	    "newpath %.5g %.5g moveto %.5g %.5g 2 0 360 arc closepath fill stroke\n",
	    p.x, p.y, p.x, p.y);
    fprintf(stderr, "/Times-Roman findfont 4 scalefont setfont\n");
    fprintf(stderr, "%.5g %.5g moveto (\\(%.5g,%.5g\\)) show\n", p.x + 5, p.y + 5,
	    p.x, p.y);
    fprintf(stderr, "grestore\n");
}
#endif

static void psprintspline(Ppolyline_t spl)
{
    int newcnt = Show_cnt + spl.pn + 4;
    int li, i;

    Show_boxes = ALLOC(newcnt+2,Show_boxes,char*);
    li = Show_cnt+1;
    Show_boxes[li++] = gv_strdup("%%!");
    Show_boxes[li++] = gv_strdup("%% spline");
    Show_boxes[li++] = gv_strdup("gsave 1 0 0 setrgbcolor newpath");
    for (i = 0; i < spl.pn; i++) {
	agxbuf buf = {0};
	agxbprint(&buf, "%f %f %s", spl.ps[i].x, spl.ps[i].y,
	  i == 0 ?  "moveto" : (i % 3 == 0 ? "curveto" : ""));
	Show_boxes[li++] = agxbdisown(&buf);
    }
    Show_boxes[li++] = gv_strdup("stroke grestore");
    Show_cnt = newcnt;
    Show_boxes[Show_cnt+1] = NULL;
}

static void psprintline(Ppolyline_t pl)
{
    int newcnt = Show_cnt + pl.pn + 4;
    int i, li;

    Show_boxes = ALLOC(newcnt+2,Show_boxes,char*);
    li = Show_cnt+1;
    Show_boxes[li++] = gv_strdup("%%!");
    Show_boxes[li++] = gv_strdup("%% line");
    Show_boxes[li++] = gv_strdup("gsave 0 0 1 setrgbcolor newpath");
    for (i = 0; i < pl.pn; i++) {
	agxbuf buf = {0};
	agxbprint(&buf, "%f %f %s", pl.ps[i].x, pl.ps[i].y,
		i == 0 ? "moveto" : "lineto");
	Show_boxes[li++] = agxbdisown(&buf);
    }
    Show_boxes[li++] = gv_strdup("stroke grestore");
    Show_cnt = newcnt;
    Show_boxes[Show_cnt+1] = NULL;
}

static void psprintpoly(Ppoly_t p)
{
    int newcnt = Show_cnt + p.pn + 3;
    point tl, hd;
    int bi, li;
    char*  pfx;

    Show_boxes = ALLOC(newcnt+2,Show_boxes,char*);
    li = Show_cnt+1;
    Show_boxes[li++] = gv_strdup("%% poly list");
    Show_boxes[li++] = gv_strdup("gsave 0 1 0 setrgbcolor");
    for (bi = 0; bi < p.pn; bi++) {
	tl.x = (int)p.ps[bi].x;
	tl.y = (int)p.ps[bi].y;
	hd.x = (int)p.ps[(bi+1) % p.pn].x;
	hd.y = (int)p.ps[(bi+1) % p.pn].y;
	if (tl.x == hd.x && tl.y == hd.y) pfx = "%%";
	else pfx ="";
	agxbuf buf = {0};
	agxbprint(&buf, "%s%d %d %d %d makevec", pfx, tl.x, tl.y, hd.x, hd.y);
	Show_boxes[li++] = agxbdisown(&buf);
    }
    Show_boxes[li++] = gv_strdup("grestore");

    Show_cnt = newcnt;
    Show_boxes[Show_cnt+1] = NULL;
}

static void psprintboxes(int boxn, boxf* boxes)
{
    int newcnt = Show_cnt + 5*boxn + 3;
    pointf ll, ur;
    int bi, li;

    Show_boxes = ALLOC(newcnt+2,Show_boxes,char*);
    li = Show_cnt+1;
    Show_boxes[li++] = gv_strdup("%% box list");
    Show_boxes[li++] = gv_strdup("gsave 0 1 0 setrgbcolor");
    for (bi = 0; bi < boxn; bi++) {
	ll = boxes[bi].LL, ur = boxes[bi].UR;
	agxbuf buf = {0};
	agxbprint(&buf, "newpath\n%.0f %.0f moveto", ll.x, ll.y);
	Show_boxes[li++] = agxbdisown(&buf);
	agxbprint(&buf, "%.0f %.0f lineto", ll.x, ur.y);
	Show_boxes[li++] = agxbdisown(&buf);
	agxbprint(&buf, "%.0f %.0f lineto", ur.x, ur.y);
	Show_boxes[li++] = agxbdisown(&buf);
	agxbprint(&buf, "%.0f %.0f lineto", ur.x, ll.y);
	Show_boxes[li++] = agxbdisown(&buf);
	Show_boxes[li++] = gv_strdup("closepath stroke");
    }
    Show_boxes[li++] = gv_strdup("grestore");

    Show_cnt = newcnt;
    Show_boxes[Show_cnt+1] = NULL;
}

static void psprintinit (int begin)
{
    int newcnt = Show_cnt + 1;

    Show_boxes = ALLOC(newcnt+2,Show_boxes,char*);
    if (begin)
	Show_boxes[1+Show_cnt] = gv_strdup("dbgstart");
    else
	Show_boxes[1+Show_cnt] = gv_strdup("grestore");
    Show_cnt = newcnt;
    Show_boxes[Show_cnt+1] = NULL;
}

static bool debugleveln(edge_t* realedge, int i)
{
    return GD_showboxes(agraphof(aghead(realedge))) == i ||
	    GD_showboxes(agraphof(agtail(realedge))) == i ||
	    ED_showboxes(realedge) == i ||
	    ND_showboxes(aghead(realedge)) == i ||
	    ND_showboxes(agtail(realedge)) == i;
}
#endif  /* DEBUG */



/* simpleSplineRoute:
 * Given a simple (ccw) polygon, route an edge from tp to hp.
 */
pointf*
simpleSplineRoute (pointf tp, pointf hp, Ppoly_t poly, int* n_spl_pts,
    int polyline)
{
    Ppolyline_t pl, spl;
    Ppoint_t eps[2];
    Pvector_t evs[2];
    int i;

    eps[0].x = tp.x;
    eps[0].y = tp.y;
    eps[1].x = hp.x;
    eps[1].y = hp.y;
    if (Pshortestpath(&poly, eps, &pl) < 0)
        return NULL;

    if (polyline)
	make_polyline (pl, &spl);
    else {
	if (poly.pn > edgen) {
	    edges = ALLOC(poly.pn, edges, Pedge_t);
	    edgen = poly.pn;
	}
	for (i = 0; i < poly.pn; i++) {
	    edges[i].a = poly.ps[i];
	    edges[i].b = poly.ps[(i + 1) % poly.pn];
	}
	    evs[0].x = evs[0].y = 0;
	    evs[1].x = evs[1].y = 0;
	if (Proutespline(edges, poly.pn, pl, evs, &spl) < 0)
            return NULL;
    }

    pointf *ps = (pointf*)calloc(spl.pn, sizeof(ps[0]));
    if (ps == NULL) {
	agerr(AGERR, "cannot allocate ps\n");
	return NULL;
    }
    for (i = 0; i < spl.pn; i++) {
        ps[i] = spl.ps[i];
    }
    *n_spl_pts = spl.pn;
    return ps;
}

/* routesplinesinit:
 * Data initialized once until matching call to routeplineterm
 * Allows recursive calls to dot
 */
int
routesplinesinit()
{
    if (++routeinit > 1) return 0;
#ifdef DEBUG
    if (Show_boxes) {
        for (int i = 0; Show_boxes[i]; i++)
	    free (Show_boxes[i]);
	free (Show_boxes);
	Show_boxes = NULL;
	Show_cnt = 0;
    }
#endif
    nedges = 0;
    nboxes = 0;
    if (Verbose)
	start_timer();
    return 0;
}

void routesplinesterm()
{
    if (--routeinit > 0) return;
    if (Verbose)
	fprintf(stderr,
		"routesplines: %d edges, %d boxes %.2f sec\n",
		nedges, nboxes, elapsed_sec());
}

static void
limitBoxes (boxf* boxes, int boxn, const pointf *pps, int pn, int delta)
{
    int bi, si, splinepi;
    double t;
    pointf sp[4];
    int num_div = delta * boxn;

    for (splinepi = 0; splinepi + 3 < pn; splinepi += 3) {
	for (si = 0; si <= num_div; si++) {
	    t = si / (double)num_div;
	    sp[0] = pps[splinepi];
	    sp[1] = pps[splinepi + 1];
	    sp[2] = pps[splinepi + 2];
	    sp[3] = pps[splinepi + 3];
	    sp[0].x += t * (sp[1].x - sp[0].x);
	    sp[0].y += t * (sp[1].y - sp[0].y);
	    sp[1].x += t * (sp[2].x - sp[1].x);
	    sp[1].y += t * (sp[2].y - sp[1].y);
	    sp[2].x += t * (sp[3].x - sp[2].x);
	    sp[2].y += t * (sp[3].y - sp[2].y);
	    sp[0].x += t * (sp[1].x - sp[0].x);
	    sp[0].y += t * (sp[1].y - sp[0].y);
	    sp[1].x += t * (sp[2].x - sp[1].x);
	    sp[1].y += t * (sp[2].y - sp[1].y);
	    sp[0].x += t * (sp[1].x - sp[0].x);
	    sp[0].y += t * (sp[1].y - sp[0].y);
	    for (bi = 0; bi < boxn; bi++) {
/* this tested ok on 64bit machines, but on 32bit we need this FUDGE
 *     or graphs/directed/records.gv fails */
#define FUDGE .0001
		if (sp[0].y <= boxes[bi].UR.y+FUDGE && sp[0].y >= boxes[bi].LL.y-FUDGE) {
		    boxes[bi].LL.x = fmin(boxes[bi].LL.x, sp[0].x);
		    boxes[bi].UR.x = fmax(boxes[bi].UR.x, sp[0].x);
		}
	    }
	}
    }
}

#define INIT_DELTA 10
#define LOOP_TRIES 15  /* number of times to try to limiting boxes to regain space, using smaller divisions */

/* routesplines:
 * Route a path using the path info in pp. This includes start and end points
 * plus a collection of contiguous boxes contain the terminal points. The boxes
 * are converted into a containing polygon. A shortest path is constructed within
 * the polygon from between the terminal points. If polyline is true, this path
 * is converted to a spline representation. Otherwise, we call the path planner to
 * convert the polyline into a smooth spline staying within the polygon. In both
 * cases, the function returns an array of the computed control points. The number
 * of these points is given in npoints.
 *
 * Note that the returned points are stored in a single array, so the points must be
 * used before another call to this function.
 *
 * During cleanup, the function determines the x-extent of the spline in the box, so
 * the box can be shrunk to the minimum width. The extra space can then be used by other
 * edges.
 *
 * If a catastrophic error, return NULL and npoints is 0.
 */
static pointf *_routesplines(path * pp, int *npoints, int polyline)
{
    Ppoly_t poly;
    Ppolyline_t pl, spl;
    int splinepi;
    Ppoint_t eps[2];
    Pvector_t evs[2];
    int edgei, prev, next;
    int pi, bi;
    boxf *boxes;
    int boxn;
    edge_t* realedge;
    bool flip;
    int loopcnt, delta = INIT_DELTA;
    bool unbounded;

    *npoints = 0;
    nedges++;
    nboxes += pp->nbox;

    for (realedge = (edge_t*)pp->data;
	 realedge && ED_edge_type(realedge) != NORMAL;
	 realedge = ED_to_orig(realedge));
    if (!realedge) {
	agerr(AGERR, "in routesplines, cannot find NORMAL edge\n");
	return NULL;
    }

    boxes = pp->boxes;
    boxn = pp->nbox;

    if (checkpath(boxn, boxes, pp))
	return NULL;

#ifdef DEBUG
    if (debugleveln(realedge, 1))
	printboxes(boxn, boxes);
    if (debugleveln(realedge, 3)) {
	psprintinit(1);
	psprintboxes(boxn, boxes);
    }
#endif

    if (boxn * 8 > polypointn) {
	polypoints = ALLOC(boxn * 8, polypoints, Ppoint_t);
	polypointn = boxn * 8;
    }

    if (boxn > 1 && boxes[0].LL.y > boxes[1].LL.y) {
        flip = true;
	for (bi = 0; bi < boxn; bi++) {
	    double v = boxes[bi].UR.y;
	    boxes[bi].UR.y = -1*boxes[bi].LL.y;
	    boxes[bi].LL.y = -v;
	}
    }
    else flip = false;

    if (agtail(realedge) != aghead(realedge)) {
	/* I assume that the path goes either down only or
	   up - right - down */
	for (bi = 0, pi = 0; bi < boxn; bi++) {
	    next = prev = 0;
	    if (bi > 0)
		prev = boxes[bi].LL.y > boxes[bi - 1].LL.y ? -1 : 1;
	    if (bi < boxn - 1)
		next = boxes[bi + 1].LL.y > boxes[bi].LL.y ? 1 : -1;
	    if (prev != next) {
		if (next == -1 || prev == 1) {
		    polypoints[pi].x = boxes[bi].LL.x;
		    polypoints[pi++].y = boxes[bi].UR.y;
		    polypoints[pi].x = boxes[bi].LL.x;
		    polypoints[pi++].y = boxes[bi].LL.y;
		} else {
		    polypoints[pi].x = boxes[bi].UR.x;
		    polypoints[pi++].y = boxes[bi].LL.y;
		    polypoints[pi].x = boxes[bi].UR.x;
		    polypoints[pi++].y = boxes[bi].UR.y;
		}
	    }
	    else if (prev == 0) { /* single box */
		polypoints[pi].x = boxes[bi].LL.x;
		polypoints[pi++].y = boxes[bi].UR.y;
		polypoints[pi].x = boxes[bi].LL.x;
		polypoints[pi++].y = boxes[bi].LL.y;
	    }
	    else {
		if (!(prev == -1 && next == -1)) {
		    agerr(AGERR, "in routesplines, illegal values of prev %d and next %d, line %d\n", prev, next, __LINE__);
		    return NULL;
		}
	    }
	}
	for (bi = boxn - 1; bi >= 0; bi--) {
	    next = prev = 0;
	    if (bi < boxn - 1)
		prev = boxes[bi].LL.y > boxes[bi + 1].LL.y ? -1 : 1;
	    if (bi > 0)
		next = boxes[bi - 1].LL.y > boxes[bi].LL.y ? 1 : -1;
	    if (prev != next) {
		if (next == -1 || prev == 1 ) {
		    polypoints[pi].x = boxes[bi].LL.x;
		    polypoints[pi++].y = boxes[bi].UR.y;
		    polypoints[pi].x = boxes[bi].LL.x;
		    polypoints[pi++].y = boxes[bi].LL.y;
		} else {
		    polypoints[pi].x = boxes[bi].UR.x;
		    polypoints[pi++].y = boxes[bi].LL.y;
		    polypoints[pi].x = boxes[bi].UR.x;
		    polypoints[pi++].y = boxes[bi].UR.y;
		}
	    }
	    else if (prev == 0) { /* single box */
		polypoints[pi].x = boxes[bi].UR.x;
		polypoints[pi++].y = boxes[bi].LL.y;
		polypoints[pi].x = boxes[bi].UR.x;
		polypoints[pi++].y = boxes[bi].UR.y;
	    }
	    else {
		if (!(prev == -1 && next == -1)) {
		    /* it went badly, e.g. degenerate box in boxlist */
		    agerr(AGERR, "in routesplines, illegal values of prev %d and next %d, line %d\n", prev, next, __LINE__);
		    return NULL; /* for correctness sake, it's best to just stop */
		}
		polypoints[pi].x = boxes[bi].UR.x;
		polypoints[pi++].y = boxes[bi].LL.y;
		polypoints[pi].x = boxes[bi].UR.x;
		polypoints[pi++].y = boxes[bi].UR.y;
		polypoints[pi].x = boxes[bi].LL.x;
		polypoints[pi++].y = boxes[bi].UR.y;
		polypoints[pi].x = boxes[bi].LL.x;
		polypoints[pi++].y = boxes[bi].LL.y;
	    }
	}
    }
    else {
	agerr(AGERR, "in routesplines, edge is a loop at %s\n", agnameof(aghead(realedge)));
	return NULL;
    }

    if (flip) {
	for (bi = 0; bi < boxn; bi++) {
	    double v = boxes[bi].UR.y;
	    boxes[bi].UR.y = -1*boxes[bi].LL.y;
	    boxes[bi].LL.y = -v;
	}
	for (int i = 0; i < pi; i++)
	    polypoints[i].y *= -1;
    }

    for (bi = 0; bi < boxn; bi++)
	boxes[bi].LL.x = INT_MAX, boxes[bi].UR.x = INT_MIN;
    poly.ps = polypoints, poly.pn = pi;
    eps[0].x = pp->start.p.x, eps[0].y = pp->start.p.y;
    eps[1].x = pp->end.p.x, eps[1].y = pp->end.p.y;
    if (Pshortestpath(&poly, eps, &pl) < 0) {
	agerr(AGERR, "in routesplines, Pshortestpath failed\n");
	return NULL;
    }
#ifdef DEBUG
    if (debugleveln(realedge, 3)) {
	psprintpoly(poly);
	psprintline(pl);
    }
#endif

    if (polyline) {
	make_polyline (pl, &spl);
    }
    else {
	if (poly.pn > edgen) {
	    edges = ALLOC(poly.pn, edges, Pedge_t);
	    edgen = poly.pn;
	}
	for (edgei = 0; edgei < poly.pn; edgei++) {
	    edges[edgei].a = polypoints[edgei];
	    edges[edgei].b = polypoints[(edgei + 1) % poly.pn];
	}
	if (pp->start.constrained) {
	    evs[0].x = cos(pp->start.theta);
	    evs[0].y = sin(pp->start.theta);
	} else
	    evs[0].x = evs[0].y = 0;
	if (pp->end.constrained) {
	    evs[1].x = -cos(pp->end.theta);
	    evs[1].y = -sin(pp->end.theta);
	} else
	    evs[1].x = evs[1].y = 0;

	if (Proutespline(edges, poly.pn, pl, evs, &spl) < 0) {
	    agerr(AGERR, "in routesplines, Proutespline failed\n");
	    return NULL;
	}
#ifdef DEBUG
	if (debugleveln(realedge, 3)) {
	    psprintspline(spl);
	    psprintinit(0);
	}
#endif
    }
    pointf *ps = (pointf*)calloc(spl.pn, sizeof(ps[0]));
    if (ps == NULL) {
	agerr(AGERR, "cannot allocate ps\n");
	return NULL;  /* Bailout if no memory left */
    }

    for (bi = 0; bi < boxn; bi++) {
	boxes[bi].LL.x = INT_MAX;
	boxes[bi].UR.x = INT_MIN;
    }
    unbounded = true;
    for (splinepi = 0; splinepi < spl.pn; splinepi++) {
	ps[splinepi] = spl.ps[splinepi];
    }

    for (loopcnt = 0; unbounded && loopcnt < LOOP_TRIES; loopcnt++) {
	limitBoxes (boxes, boxn, ps, spl.pn, delta);

    /* The following check is necessary because if a box is not very
     * high, it is possible that the sampling above might miss it.
     * Therefore, we make the sample finer until all boxes have
     * valid values. cf. bug 456. Would making sp[] pointfs help?
     */
	for (bi = 0; bi < boxn; bi++) {
	/* these fp equality tests are used only to detect if the
	 * values have been changed since initialization - ok */
	    if (boxes[bi].LL.x == INT_MAX || boxes[bi].UR.x == INT_MIN) {
		delta *= 2; /* try again with a finer interval */
		if (delta > INT_MAX/boxn) /* in limitBoxes, boxn*delta must fit in an int, so give up */
		    loopcnt = LOOP_TRIES;
		break;
	    }
	}
	if (bi == boxn)
	    unbounded = false;
    }
    if (unbounded) {
	/* Either an extremely short, even degenerate, box, or some failure with the path
         * planner causing the spline to miss some boxes. In any case, use the shortest path
	 * to bound the boxes. This will probably mean a bad edge, but we avoid an infinite
	 * loop and we can see the bad edge, and even use the showboxes scaffolding.
	 */
	Ppolyline_t polyspl;
	agerr(AGWARN, "Unable to reclaim box space in spline routing for edge \"%s\" -> \"%s\". Something is probably seriously wrong.\n", agnameof(agtail(realedge)), agnameof(aghead(realedge)));
	make_polyline (pl, &polyspl);
	limitBoxes (boxes, boxn, polyspl.ps, polyspl.pn, INIT_DELTA);
    }

    *npoints = spl.pn;

#ifdef DEBUG
    if (GD_showboxes(agraphof(aghead(realedge))) == 2 ||
	GD_showboxes(agraphof(agtail(realedge))) == 2 ||
	ED_showboxes(realedge) == 2 ||
	ND_showboxes(aghead(realedge)) == 2 ||
	ND_showboxes(agtail(realedge)) == 2)
	printboxes(boxn, boxes);
#endif

    return ps;
}

pointf *routesplines(path * pp, int *npoints)
{
    return _routesplines (pp, npoints, 0);
}

pointf *routepolylines(path * pp, int *npoints)
{
    return _routesplines (pp, npoints, 1);
}

static int overlap(int i0, int i1, int j0, int j1)
{
    /* i'll bet there's an elegant way to do this */
    if (i1 <= j0)
	return 0;
    if (i0 >= j1)
	return 0;
    if (j0 <= i0 && i0 <= j1)
	return j1 - i0;
    if (j0 <= i1 && i1 <= j1)
	return i1 - j0;
    return MIN(i1 - i0, j1 - j0);
}


/*
 * repairs minor errors in the boxpath, such as boxes not joining
 * or slightly intersecting.  it's sort of the equivalent of the
 * audit process in the 5E control program - if you've given up on
 * fixing all the bugs, at least try to engineer around them!
 * in postmodern CS, we could call this "self-healing code."
 *
 * Return 1 on failure; 0 on success.
 */
static int checkpath(int boxn, boxf* boxes, path* thepath)
{
    boxf *ba, *bb;
    int bi, i, errs, l, r, d, u;
    int xoverlap, yoverlap;

    /* remove degenerate boxes. */
    i = 0;
    for (bi = 0; bi < boxn; bi++) {
	if (fabs(boxes[bi].LL.y - boxes[bi].UR.y) < .01)
	    continue;
	if (fabs(boxes[bi].LL.x - boxes[bi].UR.x) < .01)
	    continue;
	boxes[i] = boxes[bi];
	i++;
    }
    boxn = i;

    ba = &boxes[0];
    if (ba->LL.x > ba->UR.x || ba->LL.y > ba->UR.y) {
	agerr(AGERR, "in checkpath, box 0 has LL coord > UR coord\n");
	printpath(thepath);
	return 1;
    }
    for (bi = 0; bi < boxn - 1; bi++) {
	ba = &boxes[bi], bb = &boxes[bi + 1];
	if (bb->LL.x > bb->UR.x || bb->LL.y > bb->UR.y) {
	    agerr(AGERR, "in checkpath, box %d has LL coord > UR coord\n", bi + 1);
	    printpath(thepath);
	    return 1;
	}
	l = ba->UR.x < bb->LL.x ? 1 : 0;
	r = ba->LL.x > bb->UR.x ? 1 : 0;
	d = ba->UR.y < bb->LL.y ? 1 : 0;
	u = ba->LL.y > bb->UR.y ? 1 : 0;
	errs = l + r + d + u;
	if (errs > 0 && Verbose) {
	    fprintf(stderr, "in checkpath, boxes %d and %d don't touch\n",
		    bi, bi + 1);
	    printpath(thepath);
	}
	if (errs > 0) {
	    int xy;

	    if (l == 1)
		xy = ba->UR.x, ba->UR.x = bb->LL.x, bb->LL.x = xy, l = 0;
	    else if (r == 1)
		xy = ba->LL.x, ba->LL.x = bb->UR.x, bb->UR.x = xy, r = 0;
	    else if (d == 1)
		xy = ba->UR.y, ba->UR.y = bb->LL.y, bb->LL.y = xy, d = 0;
	    else if (u == 1)
		xy = ba->LL.y, ba->LL.y = bb->UR.y, bb->UR.y = xy, u = 0;
	    for (i = 0; i < errs - 1; i++) {
		if (l == 1)
		    xy = (ba->UR.x + bb->LL.x) / 2.0 + 0.5, ba->UR.x =
			bb->LL.x = xy, l = 0;
		else if (r == 1)
		    xy = (ba->LL.x + bb->UR.x) / 2.0 + 0.5, ba->LL.x =
			bb->UR.x = xy, r = 0;
		else if (d == 1)
		    xy = (ba->UR.y + bb->LL.y) / 2.0 + 0.5, ba->UR.y =
			bb->LL.y = xy, d = 0;
		else if (u == 1)
		    xy = (ba->LL.y + bb->UR.y) / 2.0 + 0.5, ba->LL.y =
			bb->UR.y = xy, u = 0;
	    }
	}
	/* check for overlapping boxes */
	xoverlap = overlap(ba->LL.x, ba->UR.x, bb->LL.x, bb->UR.x);
	yoverlap = overlap(ba->LL.y, ba->UR.y, bb->LL.y, bb->UR.y);
	if (xoverlap && yoverlap) {
	    if (xoverlap < yoverlap) {
		if (ba->UR.x - ba->LL.x > bb->UR.x - bb->LL.x) {
		    /* take space from ba */
		    if (ba->UR.x < bb->UR.x)
			ba->UR.x = bb->LL.x;
		    else
			ba->LL.x = bb->UR.x;
		} else {
		    /* take space from bb */
		    if (ba->UR.x < bb->UR.x)
			bb->LL.x = ba->UR.x;
		    else
			bb->UR.x = ba->LL.x;
		}
	    } else {		/* symmetric for y coords */
		if (ba->UR.y - ba->LL.y > bb->UR.y - bb->LL.y) {
		    /* take space from ba */
		    if (ba->UR.y < bb->UR.y)
			ba->UR.y = bb->LL.y;
		    else
			ba->LL.y = bb->UR.y;
		} else {
		    /* take space from bb */
		    if (ba->UR.y < bb->UR.y)
			bb->LL.y = ba->UR.y;
		    else
			bb->UR.y = ba->LL.y;
		}
	    }
	}
    }

    if (thepath->start.p.x < boxes[0].LL.x
	|| thepath->start.p.x > boxes[0].UR.x
	|| thepath->start.p.y < boxes[0].LL.y
	|| thepath->start.p.y > boxes[0].UR.y) {
	if (Verbose) {
	    fprintf(stderr, "in checkpath, start port not in first box\n");
	    printpath(thepath);
	}
	thepath->start.p.x = fmax(thepath->start.p.x, boxes[0].LL.x);
	thepath->start.p.x = fmin(thepath->start.p.x, boxes[0].UR.x);
	thepath->start.p.y = fmax(thepath->start.p.y, boxes[0].LL.y);
	thepath->start.p.y = fmin(thepath->start.p.y, boxes[0].UR.y);
    }
    if (thepath->end.p.x < boxes[boxn - 1].LL.x
	|| thepath->end.p.x > boxes[boxn - 1].UR.x
	|| thepath->end.p.y < boxes[boxn - 1].LL.y
	|| thepath->end.p.y > boxes[boxn - 1].UR.y) {
	if (Verbose) {
	    fprintf(stderr, "in checkpath, end port not in last box\n");
	    printpath(thepath);
	}
	thepath->end.p.x = fmax(thepath->end.p.x, boxes[boxn - 1].LL.x);
	thepath->end.p.x = fmin(thepath->end.p.x, boxes[boxn - 1].UR.x);
	thepath->end.p.y = fmax(thepath->end.p.y, boxes[boxn - 1].LL.y);
	thepath->end.p.y = fmin(thepath->end.p.y, boxes[boxn - 1].UR.y);
    }
    return 0;
}

static void printpath(path * pp)
{
    int bi;

    fprintf(stderr, "%d boxes:\n", pp->nbox);
    for (bi = 0; bi < pp->nbox; bi++)
	fprintf(stderr, "%d (%.5g, %.5g), (%.5g, %.5g)\n", bi,
		pp->boxes[bi].LL.x, pp->boxes[bi].LL.y,
	       	pp->boxes[bi].UR.x, pp->boxes[bi].UR.y);
    fprintf(stderr, "start port: (%.5g, %.5g), tangent angle: %.5g, %s\n",
	    pp->start.p.x, pp->start.p.y, pp->start.theta,
	    pp->start.constrained ? "constrained" : "not constrained");
    fprintf(stderr, "end port: (%.5g, %.5g), tangent angle: %.5g, %s\n",
	    pp->end.p.x, pp->end.p.y, pp->end.theta,
	    pp->end.constrained ? "constrained" : "not constrained");
}

static pointf get_centroid(Agraph_t *g)
{
    pointf sum = {0.0, 0.0};

    sum.x = (GD_bb(g).LL.x + GD_bb(g).UR.x) / 2.0;
    sum.y = (GD_bb(g).LL.y + GD_bb(g).UR.y) / 2.0;
    return sum;
}

//generic vector structure
typedef struct _tag_vec
{
    void** _mem;
    size_t _elems;
    size_t _capelems;
} vec;

static vec* vec_new(void)
{
    vec* pvec = (vec*)malloc(sizeof(vec));
    pvec->_capelems = 10;
    pvec->_elems = 0;
    pvec->_mem = (void**)malloc(pvec->_capelems * sizeof(void*));
    return pvec;
}

static size_t vec_length(const vec* pvec)
{
    return pvec->_elems;
}

static void* vec_get(vec* pvec, size_t index)
{
    assert(index < pvec->_elems);
    return pvec->_mem[index];
}

static void vec_delete(vec* pvec)
{
    free(pvec->_mem);
    free(pvec);
}

// cycles is assumed to be a vec of vec of nodes.
static void cycles_delete(vec* cycles) {
  for (size_t i = 0; i < vec_length(cycles); ++i) {
    vec_delete((vec*)vec_get(cycles, i));
  }
  vec_delete(cycles);
}

static void vec_push_back(vec* pvec, void* data)
{
    if (pvec->_elems == pvec->_capelems) {
		pvec->_capelems += 10;
		pvec->_mem = (void**)realloc(pvec->_mem, pvec->_capelems * sizeof(void*));
	}
    pvec->_mem[pvec->_elems++] = data;
}

static void* vec_pop(vec* pvec)
{
	if (pvec->_elems > 0)
		return pvec->_mem[--pvec->_elems];
	return NULL;
}

static bool vec_contains(vec* pvec, void* item)
{
	for (size_t i=0; i < pvec->_elems; ++i) {
		if (pvec->_mem[i] == item)
			return true;
	}

	return false;
}

static vec* vec_copy(vec* pvec)
{
    vec* nvec = (vec*)malloc(sizeof(vec));
    nvec->_capelems = pvec->_capelems;
    nvec->_elems = pvec->_elems;
    nvec->_mem = (void**)malloc(pvec->_capelems * sizeof(void*));
	memcpy(nvec->_mem, pvec->_mem, pvec->_elems * sizeof(void*));
    return nvec;
}
//end generic vector structure

static bool cycle_contains_edge(vec* cycle, edge_t* edge)
{
	node_t* start = agtail(edge);
	node_t* end = aghead(edge);
	node_t* c_start;
	node_t* c_end;

	size_t cycle_len = vec_length(cycle);

	for (size_t i=0; i < cycle_len; ++i) {
		if (i == 0) {
			c_start = (node_t*)vec_get(cycle, cycle_len-1);
		} else {
			c_start = (node_t*)vec_get(cycle, i-1);
		}

		c_end = (node_t*)vec_get(cycle, i);

		if (c_start == start && c_end == end)
			return true;
	}


	return false;
}

static bool is_cycle_unique(vec* cycles, vec* cycle)
{
	size_t cycle_len = vec_length(cycle);
	size_t n_cycles = vec_length(cycles);
	size_t c; //cycles counter
	size_t i; //node counter

	vec* cur_cycle;
	size_t cur_cycle_len;
	void* cur_cycle_item;
	bool all_items_match;

	for (c=0; c < n_cycles; ++c) {
		cur_cycle = (vec*)vec_get(cycles, c);
		cur_cycle_len = vec_length(cur_cycle);

		//if all the items match in equal length cycles then we're not unique
		if (cur_cycle_len == cycle_len) {
			all_items_match = true;
			for (i=0; i < cur_cycle_len; ++i) {
				cur_cycle_item = vec_get(cur_cycle, i);
				if (!vec_contains(cycle, cur_cycle_item)) {
					all_items_match = false;
					break;
				}
			}
			if (all_items_match)
				return false;
		}
	}

	return true;
}

static void dfs(graph_t *g, node_t* search, vec* visited, node_t* end, vec* cycles)
{
	edge_t* e;
	node_t* n;

	if (vec_contains(visited, search)) {
		if (search == end) {
			if (is_cycle_unique(cycles, visited)) {
				vec* cycle = vec_copy(visited);
				vec_push_back(cycles, cycle);
			}
		}
	} else {
		vec_push_back(visited, search);
		for (e = agfstout(g, search); e; e = agnxtout(g, e)) {
			n = aghead(e);
			dfs(g, n, visited, end, cycles);
		}
		vec_pop(visited);
	}
}

// Returns a vec of vec of nodes (aka a vector of cycles), which must be freed using cycles_delete.
static vec* find_all_cycles(graph_t *g)
{
    node_t *n;

    vec* alloced_cycles = vec_new(); //vector of vectors of nodes -- AKA cycles to delete
    vec* cycles = vec_new(); //vector of vectors of nodes AKA a vector of cycles
    vec* cycle;

    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
		cycle = vec_new();
		vec_push_back(alloced_cycles, cycle); //keep track of all items we allocate to clean up at the end of this function

		dfs(g, n, cycle, n, cycles);
	}

	cycles_delete(alloced_cycles); //cycles contains copied vecs
    return cycles;
}

static vec* find_shortest_cycle_with_edge(vec* cycles, edge_t* edge, size_t min_size)
{
	size_t c; //cycle counter
	size_t cycles_len = vec_length(cycles);
	vec* cycle;
	size_t cycle_len;
	vec* shortest = 0;

	for (c=0; c < cycles_len; ++c) {
		cycle = (vec*)vec_get(cycles, c);
		cycle_len = vec_length(cycle);

		if (cycle_len < min_size)
			continue;

		if (!shortest || vec_length(shortest) > cycle_len) {
			if (cycle_contains_edge(cycle, edge)) {
				shortest = cycle;
			}
		}
	}
	return shortest;
}

static pointf get_cycle_centroid(graph_t *g, edge_t* edge)
{
	vec* cycles = find_all_cycles(g);

	//find the center of the shortest cycle containing this edge
	//cycles of length 2 do their own thing, we want 3 or
	vec* cycle = find_shortest_cycle_with_edge(cycles, edge, 3);
	size_t cycle_len;
	size_t cnt = 0;
    pointf sum = {0.0, 0.0};
	size_t idx; //edge index
	node_t *n;

	if (cycle == NULL) {
		if (cycles != NULL)
			cycles_delete(cycles);
		return get_centroid(g);
	}

	cycle_len = vec_length(cycle);

	for (idx=0; idx < cycle_len; ++idx) {
		n = (node_t*)vec_get(cycle, idx);
		sum.x += ND_coord(n).x;
        sum.y += ND_coord(n).y;
        cnt++;
	}

	if (cycles != NULL)
		cycles_delete(cycles);

	sum.x /= cnt;
    sum.y /= cnt;
    return sum;
}

static void bend(pointf spl[4], pointf centroid)
{
    pointf  midpt,a;
    double  r;
    double  dist,dx,dy;

    midpt.x = (spl[0].x + spl[3].x)/2.0;
    midpt.y = (spl[0].y + spl[3].y)/2.0;
    dx = spl[3].x - spl[0].x;
    dy = spl[3].y - spl[0].y;
    dist = hypot(dx, dy);
    r = dist/5.0;
    {
        double vX = centroid.x - midpt.x;
        double vY = centroid.y - midpt.y;
        double magV = hypot(vX, vY);
	if (magV == 0) return;  /* if midpoint == centroid, don't divide by zero */
        a.x = midpt.x - vX / magV * r;      /* + would be closest point */
        a.y = midpt.y - vY / magV * r;
    }
    /* this can be improved */
    spl[1].x = spl[2].x = a.x;
    spl[1].y = spl[2].y = a.y;
}

/* makeStraightEdge:
 *
 * FIX: handle ports on boundary?
 */
void
makeStraightEdge(graph_t * g, edge_t * e, int et, splineInfo* sinfo)
{
    edge_t *e0;
    int i, e_cnt;

    e_cnt = 1;
    e0 = e;
    while (e0 != ED_to_virt(e0) && (e0 = ED_to_virt(e0))) e_cnt++;

    edge_t **edges = N_NEW(e_cnt, edge_t*);
    e0 = e;
    for (i = 0; i < e_cnt; i++) {
	edges[i] = e0;
	e0 = ED_to_virt(e0);
    }
    makeStraightEdges (g, edges, e_cnt, et, sinfo);
    free(edges);
}

void
makeStraightEdges(graph_t * g, edge_t** edges, int e_cnt, int et, splineInfo* sinfo)
{
    pointf dumb[4];
    node_t *n;
    node_t *head;
    bool curved = et == EDGETYPE_CURVED;
    pointf perp;
    pointf del;
    edge_t *e0;
    edge_t *e;
    int i, j, xstep, dx;
    double l_perp;
    pointf dumber[4];

    e = edges[0];
    n = agtail(e);
    head = aghead(e);
    dumb[1] = dumb[0] = add_pointf(ND_coord(n), ED_tail_port(e).p);
    dumb[2] = dumb[3] = add_pointf(ND_coord(head), ED_head_port(e).p);
    if (e_cnt == 1 || Concentrate) {
	if (curved) bend(dumb,get_cycle_centroid(g, edges[0]));
	clip_and_install(e, aghead(e), dumb, 4, sinfo);
	addEdgeLabels(e);
	return;
    }

    e0 = e;
    if (APPROXEQPT(dumb[0], dumb[3], MILLIPOINT)) {
	/* degenerate case */
	dumb[1] = dumb[0];
	dumb[2] = dumb[3];
	del.x = 0;
	del.y = 0;
    }
    else {
        perp.x = dumb[0].y - dumb[3].y;
        perp.y = dumb[3].x - dumb[0].x;
	l_perp = LEN(perp.x, perp.y);
	xstep = GD_nodesep(g->root);
	dx = xstep * (e_cnt - 1) / 2;
	dumb[1].x = dumb[0].x + dx * perp.x / l_perp;
	dumb[1].y = dumb[0].y + dx * perp.y / l_perp;
	dumb[2].x = dumb[3].x + dx * perp.x / l_perp;
	dumb[2].y = dumb[3].y + dx * perp.y / l_perp;
	del.x = -xstep * perp.x / l_perp;
	del.y = -xstep * perp.y / l_perp;
    }

    for (i = 0; i < e_cnt; i++) {
	e0 = edges[i];
	if (aghead(e0) == head) {
	    for (j = 0; j < 4; j++) {
		dumber[j] = dumb[j];
	    }
	} else {
	    for (j = 0; j < 4; j++) {
		dumber[3 - j] = dumb[j];
	    }
	}
	if (et == EDGETYPE_PLINE) {
	    Ppoint_t pts[4];
	    Ppolyline_t spl, line;

	    line.pn = 4;
	    line.ps = pts;
	    for (j=0; j < 4; j++) {
		pts[j] = dumber[j];
	    }
	    make_polyline (line, &spl);
	    clip_and_install(e0, aghead(e0), spl.ps, spl.pn, sinfo);
	}
	else
	    clip_and_install(e0, aghead(e0), dumber, 4, sinfo);

	addEdgeLabels(e0);
	dumb[1].x += del.x;
	dumb[1].y += del.y;
	dumb[2].x += del.x;
	dumb[2].y += del.y;
    }
}
