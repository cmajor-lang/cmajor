/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "../cgraph/alloc.h"
#include "../cgraph/likely.h"
#include "vis.h"

typedef Ppoint_t ilcoord_t;

#ifdef DEBUG
static void printVconfig(vconfig_t * cp);
static void printVis(char *lbl, COORD * vis, int n);
static void printDad(int *vis, int n);
#endif

#ifdef GASP
static void gasp_print_obstacles(vconfig_t * conf);
static void gasp_print_point(Ppoint_t p);
static void gasp_print_polyline(Ppolyline_t * route);
static void gasp_print_bezier(Ppolyline_t * route);
#endif

vconfig_t *Pobsopen(Ppoly_t ** obs, int n_obs)
{
    vconfig_t *rv;
    int poly_i, pt_i, i;
    int start, end;

    rv = (vconfig_t*)malloc(sizeof(vconfig_t));
    if (!rv) {
	return NULL;
    }

    /* get storage */
    size_t n = 0;
    for (poly_i = 0; poly_i < n_obs; poly_i++) {
	assert(obs[poly_i]->pn >= 0);
	n += (size_t)obs[poly_i]->pn;
    }
    if (n > INT_MAX) { // will this overflow rv->N?
	free(rv);
	return NULL;
    }
    rv->P = (Ppoint_t*)calloc(n, sizeof(Ppoint_t));
    assert(n_obs >= 0);
    rv->start = (int*) calloc((size_t)n_obs + 1, sizeof(int));
    rv->next = (int*) calloc(n, sizeof(int));
    rv->prev = (int*) calloc(n, sizeof(int));
    rv->N = (int)n;
    rv->Npoly = n_obs;

    // bail out if any above allocations failed
    if (UNLIKELY(rv->start == NULL || (n > 0 && (rv->P == NULL ||
                                                 rv->next == NULL ||
                                                 rv->prev == NULL)))) {
	free(rv->prev);
	free(rv->next);
	free(rv->start);
	free(rv->P);
	free(rv);
	return NULL;
    }

    /* build arrays */
    i = 0;
    for (poly_i = 0; poly_i < n_obs; poly_i++) {
	start = i;
	rv->start[poly_i] = start;
	end = start + obs[poly_i]->pn - 1;
	for (pt_i = 0; pt_i < obs[poly_i]->pn; pt_i++) {
	    rv->P[i] = obs[poly_i]->ps[pt_i];
	    rv->next[i] = i + 1;
	    rv->prev[i] = i - 1;
	    i++;
	}
	rv->next[end] = start;
	rv->prev[start] = end;
    }
    rv->start[poly_i] = i;
    visibility(rv);
    return rv;
}

void Pobsclose(vconfig_t * config)
{
    free(config->P);
    free(config->start);
    free(config->next);
    free(config->prev);
    if (config->vis) {
	free(config->vis[0]);
	free(config->vis);
    }
    free(config);
}

int Pobspath(vconfig_t * config, Ppoint_t p0, int poly0, Ppoint_t p1,
	     int poly1, Ppolyline_t * output_route)
{
    int i, *dad;
    size_t opn;
    Ppoint_t *ops;
    COORD *ptvis0, *ptvis1;

#ifdef GASP
    gasp_print_obstacles(config);
#endif
    ptvis0 = ptVis(config, poly0, p0);
    ptvis1 = ptVis(config, poly1, p1);

#ifdef GASP
    gasp_print_point(p0);
    gasp_print_point(p1);
#endif
    dad = makePath(p0, poly0, ptvis0, p1, poly1, ptvis1, config);

    opn = 1;
    for (i = dad[config->N]; i != config->N + 1; i = dad[i])
	opn++;
    opn++;
    ops = (Ppoint_t*)gv_calloc(opn, sizeof(Ppoint_t));

    size_t j = opn - 1;
    ops[j--] = p1;
    for (i = dad[config->N]; i != config->N + 1; i = dad[i])
	ops[j--] = config->P[i];
    ops[j] = p0;
    assert(j == 0);

#ifdef DEBUG
    printVconfig(config);
    printVis("p", ptvis0, config->N + 1);
    printVis("q", ptvis1, config->N + 1);
    printDad(dad, config->N + 1);
#endif

    free(ptvis0);
    free(ptvis1);

    assert(opn <= INT_MAX);
    output_route->pn = (int)opn;
    output_route->ps = ops;
#ifdef GASP
    gasp_print_polyline(output_route);
#endif
    free(dad);
    return TRUE;
}

#ifdef DEBUG
static void printVconfig(vconfig_t * cp)
{
    int i, j;
    int *next, *prev;
    Ppoint_t *pts;
    array2 arr;

    next = cp->next;
    prev = cp->prev;
    pts = cp->P;
    arr = cp->vis;

    printf("this next prev point\n");
    for (i = 0; i < cp->N; i++)
	printf("%3d  %3d  %3d    (%3g,%3g)\n", i, next[i], prev[i],
	       pts[i].x, pts[i].y);

    printf("\n\n");

    for (i = 0; i < cp->N; i++) {
	for (j = 0; j < cp->N; j++)
	    printf("%4.1f ", arr[i][j]);
	printf("\n");
    }
}

static void printVis(char *lbl, COORD * vis, int n)
{
    int i;

    printf("%s: ", lbl);
    for (i = 0; i < n; i++)
	printf("%4.1f ", vis[i]);
    printf("\n");
}

static void printDad(int *vis, int n)
{
    int i;

    printf("     ");
    for (i = 0; i < n; i++) {
	printf("%3d ", i);
    }
    printf("\n");
    printf("dad: ");
    for (i = 0; i < n; i++) {
	printf("%3d ", vis[i]);
    }
    printf("\n");
}
#endif

#ifdef GASP

static Ppoint_t Bezpt[1000];
static int Bezctr;

static void addpt(Ppoint_t p)
{
    if ((Bezctr == 0) ||
	(Bezpt[Bezctr - 1].x != p.x) || (Bezpt[Bezctr - 1].y != p.y))
	Bezpt[Bezctr++] = p;
}

#define W_DEGREE 5
static ilcoord_t Bezier(ilcoord_t * V, int degree, double t,
			ilcoord_t * Left, ilcoord_t * Right)
{
    int i, j;			/* Index variables  */
    ilcoord_t Vtemp[W_DEGREE + 1][W_DEGREE + 1];

    /* Copy control points  */
    for (j = 0; j <= degree; j++) {
	Vtemp[0][j] = V[j];
    }

    /* Triangle computation */
    for (i = 1; i <= degree; i++) {
	for (j = 0; j <= degree - i; j++) {
	    Vtemp[i][j].x =
		(1.0 - t) * Vtemp[i - 1][j].x + t * Vtemp[i - 1][j + 1].x;
	    Vtemp[i][j].y =
		(1.0 - t) * Vtemp[i - 1][j].y + t * Vtemp[i - 1][j + 1].y;
	}
    }

    if (Left != NULL)
	for (j = 0; j <= degree; j++)
	    Left[j] = Vtemp[j][0];
    if (Right != NULL)
	for (j = 0; j <= degree; j++)
	    Right[j] = Vtemp[degree - j][j];
    return (Vtemp[degree][0]);
}

static void append_bezier(Ppoint_t * bezier)
{
    double a;
    ilcoord_t left[4], right[4];

    a = fabs(area2(bezier[0], bezier[1], bezier[2]))
	+ fabs(area2(bezier[2], bezier[3], bezier[0]));
    if (a < .5) {
	addpt(bezier[0]);
	addpt(bezier[3]);
    } else {
	(void) Bezier(bezier, 3, .5, left, right);
	append_bezier(left);
	append_bezier(right);
    }
}

FILE *GASPout = stderr;

static void gasp_print_point(Ppoint_t p)
{
    fprintf(GASPout, "%3g %3g\n", p.x, p.y);
}

void gasp_print_obstacles(vconfig_t * conf)
{
    int i, j;
    Ppoly_t poly;

    fprintf(GASPout, "%d\n", conf->Npoly);
    for (i = 0; i < conf->Npoly; i++) {
	poly.ps = &(conf->P[conf->start[i]]);
	poly.pn = conf->start[i + 1] - conf->start[i];
	fprintf(GASPout, "%d\n", poly.pn);
	for (j = 0; j < poly.pn; j++)
	    gasp_print_point(poly.ps[j]);
    }
}

void gasp_print_bezier(Ppolyline_t * route)
{
    int i;

    Bezctr = 0;
    for (i = 0; i + 3 < route->pn; i += 3)
	append_bezier(route->ps + i);
    fprintf(GASPout, "%d\n", Bezctr);
    for (i = 0; i < Bezctr; i++)
	gasp_print_point(Bezpt[i]);
    Bezctr = 0;
}

void gasp_print_polyline(Ppolyline_t * route)
{
    int i;

    fprintf(GASPout, "%d\n", route->pn);
    for (i = 0; i < route->pn; i++)
	gasp_print_point(route->ps[i]);
}
#endif
