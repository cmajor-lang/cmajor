/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#define ISCCW 1
#define ISCW  2
#define ISON  3

#define DQ_FRONT 1
#define DQ_BACK  2

#define prerror(msg) \
        fprintf (stderr, "libpath/%s:%d: %s\n", __FILE__, __LINE__, (msg))

#define POINTSIZE sizeof (Ppoint_t)

typedef struct pointnlink_t {
    Ppoint_t *pp;
    struct pointnlink_t *link;
} pointnlink_t;

#define POINTNLINKSIZE sizeof (pointnlink_t)
#define POINTNLINKPSIZE sizeof (pointnlink_t *)

typedef struct tedge_t {
    pointnlink_t *pnl0p;
    pointnlink_t *pnl1p;
    struct triangle_t *ltp;
    struct triangle_t *rtp;
} tedge_t;

typedef struct triangle_t {
    int mark;
    struct tedge_t e[3];
} triangle_t;

#define TRIANGLESIZE sizeof (triangle_t)

typedef struct deque_t {
    pointnlink_t **pnlps;
    int pnlpn, fpnlpi, lpnlpi, apex;
} deque_t;

static pointnlink_t *pnls, **pnlps;
static size_t pnln;
static int pnll;

static triangle_t *tris;
static size_t trin;
static int tril;

static deque_t dq;

static Ppoint_t *ops;
static int opn;

static int triangulate(pointnlink_t **, int);
static bool isdiagonal(int, int, pointnlink_t **, int);
static int loadtriangle(pointnlink_t *, pointnlink_t *, pointnlink_t *);
static void connecttris(long, long);
static bool marktripath(long, long);

static void add2dq(int, pointnlink_t *);
static void splitdq(int, int);
static int finddqsplit(pointnlink_t *);

static int ccw(Ppoint_t *, Ppoint_t *, Ppoint_t *);
static bool intersects(Ppoint_t *, Ppoint_t *, Ppoint_t *, Ppoint_t *);
static bool between(Ppoint_t *, Ppoint_t *, Ppoint_t *);
static int pointintri(long, Ppoint_t *);

static int growpnls(size_t);
static int growtris(size_t);
static int growdq(int);
static int growops2(int);

/* Pshortestpath:
 * Find a shortest path contained in the polygon polyp going between the
 * points supplied in eps. The resulting polyline is stored in output.
 * Return 0 on success, -1 on bad input, -2 on memory allocation problem.
 */
int Pshortestpath(Ppoly_t * polyp, Ppoint_t eps[2], Ppolyline_t * output)
{
    int pi, minpi;
    double minx;
    Ppoint_t p1, p2, p3;
    long trii, trij, ftrii, ltrii;
    int ei;
    pointnlink_t epnls[2], *lpnlp, *rpnlp, *pnlp;
    triangle_t *trip;
    int splitindex;
#ifdef DEBUG
    int pnli;
#endif

    /* make space */
    assert(polyp->pn >= 0);
    if (growpnls((size_t)polyp->pn) != 0)
	return -2;
    pnll = 0;
    tril = 0;
    if (growdq(polyp->pn * 2) != 0)
	return -2;
    dq.fpnlpi = dq.pnlpn / 2, dq.lpnlpi = dq.fpnlpi - 1;

    /* make sure polygon is CCW and load pnls array */
    for (pi = 0, minx = HUGE_VAL, minpi = -1; pi < polyp->pn; pi++) {
	if (minx > polyp->ps[pi].x)
	    minx = polyp->ps[pi].x, minpi = pi;
    }
    p2 = polyp->ps[minpi];
    p1 = polyp->ps[minpi == 0 ? polyp->pn - 1 : minpi - 1];
    p3 = polyp->ps[(minpi == polyp->pn - 1) ? 0 : minpi + 1];
    if ((p1.x == p2.x && p2.x == p3.x && p3.y > p2.y) ||
	ccw(&p1, &p2, &p3) != ISCCW) {
	for (pi = polyp->pn - 1; pi >= 0; pi--) {
	    if (pi < polyp->pn - 1
		&& polyp->ps[pi].x == polyp->ps[pi + 1].x
		&& polyp->ps[pi].y == polyp->ps[pi + 1].y)
		continue;
	    pnls[pnll].pp = &polyp->ps[pi];
	    pnls[pnll].link = &pnls[pnll % polyp->pn];
	    pnlps[pnll] = &pnls[pnll];
	    pnll++;
	}
    } else {
	for (pi = 0; pi < polyp->pn; pi++) {
	    if (pi > 0 && polyp->ps[pi].x == polyp->ps[pi - 1].x &&
		polyp->ps[pi].y == polyp->ps[pi - 1].y)
		continue;
	    pnls[pnll].pp = &polyp->ps[pi];
	    pnls[pnll].link = &pnls[pnll % polyp->pn];
	    pnlps[pnll] = &pnls[pnll];
	    pnll++;
	}
    }

#if defined(DEBUG) && DEBUG >= 1
    fprintf(stderr, "points\n%d\n", pnll);
    for (pnli = 0; pnli < pnll; pnli++)
	fprintf(stderr, "%f %f\n", pnls[pnli].pp->x, pnls[pnli].pp->y);
#endif

    /* generate list of triangles */
    if (triangulate(pnlps, pnll))
	return -2;

#if defined(DEBUG) && DEBUG >= 2
    fprintf(stderr, "triangles\n%d\n", tril);
    for (trii = 0; trii < tril; trii++)
	for (ei = 0; ei < 3; ei++)
	    fprintf(stderr, "%f %f\n", tris[trii].e[ei].pnl0p->pp->x,
		    tris[trii].e[ei].pnl0p->pp->y);
#endif

    /* connect all pairs of triangles that share an edge */
    for (trii = 0; trii < tril; trii++)
	for (trij = trii + 1; trij < tril; trij++)
	    connecttris(trii, trij);

    /* find first and last triangles */
    for (trii = 0; trii < tril; trii++)
	if (pointintri(trii, &eps[0]))
	    break;
    if (trii == tril) {
	prerror("source point not in any triangle");
	return -1;
    }
    ftrii = trii;
    for (trii = 0; trii < tril; trii++)
	if (pointintri(trii, &eps[1]))
	    break;
    if (trii == tril) {
	prerror("destination point not in any triangle");
	return -1;
    }
    ltrii = trii;

    /* mark the strip of triangles from eps[0] to eps[1] */
    if (!marktripath(ftrii, ltrii)) {
	prerror("cannot find triangle path");
	/* a straight line is better than failing */
	if (growops2(2) != 0)
		return -2;
	output->pn = 2;
	ops[0] = eps[0], ops[1] = eps[1];
	output->ps = ops;
	return 0;
    }

    /* if endpoints in same triangle, use a single line */
    if (ftrii == ltrii) {
	if (growops2(2) != 0)
		return -2;
	output->pn = 2;
	ops[0] = eps[0], ops[1] = eps[1];
	output->ps = ops;
	return 0;
    }

    /* build funnel and shortest path linked list (in add2dq) */
    epnls[0].pp = &eps[0], epnls[0].link = NULL;
    epnls[1].pp = &eps[1], epnls[1].link = NULL;
    add2dq(DQ_FRONT, &epnls[0]);
    dq.apex = dq.fpnlpi;
    trii = ftrii;
    while (trii != -1) {
	trip = &tris[trii];
	trip->mark = 2;

	/* find the left and right points of the exiting edge */
	for (ei = 0; ei < 3; ei++)
	    if (trip->e[ei].rtp && trip->e[ei].rtp->mark == 1)
		break;
	if (ei == 3) {		/* in last triangle */
	    if (ccw(&eps[1], dq.pnlps[dq.fpnlpi]->pp,
		    dq.pnlps[dq.lpnlpi]->pp) == ISCCW)
		lpnlp = dq.pnlps[dq.lpnlpi], rpnlp = &epnls[1];
	    else
		lpnlp = &epnls[1], rpnlp = dq.pnlps[dq.lpnlpi];
	} else {
	    pnlp = trip->e[(ei + 1) % 3].pnl1p;
	    if (ccw(trip->e[ei].pnl0p->pp, pnlp->pp,
		    trip->e[ei].pnl1p->pp) == ISCCW)
		lpnlp = trip->e[ei].pnl1p, rpnlp = trip->e[ei].pnl0p;
	    else
		lpnlp = trip->e[ei].pnl0p, rpnlp = trip->e[ei].pnl1p;
	}

	/* update deque */
	if (trii == ftrii) {
	    add2dq(DQ_BACK, lpnlp);
	    add2dq(DQ_FRONT, rpnlp);
	} else {
	    if (dq.pnlps[dq.fpnlpi] != rpnlp
		&& dq.pnlps[dq.lpnlpi] != rpnlp) {
		/* add right point to deque */
		splitindex = finddqsplit(rpnlp);
		splitdq(DQ_BACK, splitindex);
		add2dq(DQ_FRONT, rpnlp);
		/* if the split is behind the apex, then reset apex */
		if (splitindex > dq.apex)
		    dq.apex = splitindex;
	    } else {
		/* add left point to deque */
		splitindex = finddqsplit(lpnlp);
		splitdq(DQ_FRONT, splitindex);
		add2dq(DQ_BACK, lpnlp);
		/* if the split is in front of the apex, then reset apex */
		if (splitindex < dq.apex)
		    dq.apex = splitindex;
	    }
	}
	trii = -1;
	for (ei = 0; ei < 3; ei++)
	    if (trip->e[ei].rtp && trip->e[ei].rtp->mark == 1) {
		trii = trip->e[ei].rtp - tris;
		break;
	    }
    }

#if defined(DEBUG) && DEBUG >= 1
    fprintf(stderr, "polypath");
    for (pnlp = &epnls[1]; pnlp; pnlp = pnlp->link)
	fprintf(stderr, " %f %f", pnlp->pp->x, pnlp->pp->y);
    fprintf(stderr, "\n");
#endif

    for (pi = 0, pnlp = &epnls[1]; pnlp; pnlp = pnlp->link)
	pi++;
    if (growops2(pi) != 0)
	return -2;
    output->pn = pi;
    for (pi = pi - 1, pnlp = &epnls[1]; pnlp; pi--, pnlp = pnlp->link)
	ops[pi] = *pnlp->pp;
    output->ps = ops;

    return 0;
}

/* triangulate polygon */
static int triangulate(pointnlink_t **points, int point_count) {
    int pnli, pnlip1, pnlip2;

	if (point_count > 3)
	{
		for (pnli = 0; pnli < point_count; pnli++)
		{
			pnlip1 = (pnli + 1) % point_count;
			pnlip2 = (pnli + 2) % point_count;
			if (isdiagonal(pnli, pnlip2, points, point_count))
			{
				if (loadtriangle(points[pnli], points[pnlip1], points[pnlip2]) != 0)
					return -1;
				for (pnli = pnlip1; pnli < point_count - 1; pnli++)
					points[pnli] = points[pnli + 1];
				return triangulate(points, point_count - 1);
			}
		}
		prerror("triangulation failed");
    }
	else {
		if (loadtriangle(points[0], points[1], points[2]) != 0)
			return -1;
	}

    return 0;
}

/* check if (i, i + 2) is a diagonal */
static bool isdiagonal(int pnli, int pnlip2, pointnlink_t **points,
                       int point_count) {
    int pnlip1, pnlim1, pnlj, pnljp1, res;

    /* neighborhood test */
    pnlip1 = (pnli + 1) % point_count;
    pnlim1 = (pnli + point_count - 1) % point_count;
    /* If P[pnli] is a convex vertex [ pnli+1 left of (pnli-1,pnli) ]. */
    if (ccw(points[pnlim1]->pp, points[pnli]->pp, points[pnlip1]->pp) == ISCCW)
	res = ccw(points[pnli]->pp, points[pnlip2]->pp, points[pnlim1]->pp) == ISCCW
	   && ccw(points[pnlip2]->pp, points[pnli]->pp, points[pnlip1]->pp) == ISCCW;
    /* Assume (pnli - 1, pnli, pnli + 1) not collinear. */
    else
	res = ccw(points[pnli]->pp, points[pnlip2]->pp, points[pnlip1]->pp) == ISCW;
    if (!res)
	return false;

    /* check against all other edges */
    for (pnlj = 0; pnlj < point_count; pnlj++) {
	pnljp1 = (pnlj + 1) % point_count;
	if (!(pnlj == pnli || pnljp1 == pnli || pnlj == pnlip2 || pnljp1 == pnlip2))
	    if (intersects(points[pnli]->pp, points[pnlip2]->pp,
			   points[pnlj]->pp, points[pnljp1]->pp))
		return false;
    }
    return true;
}

static int loadtriangle(pointnlink_t * pnlap, pointnlink_t * pnlbp,
			 pointnlink_t * pnlcp)
{
    triangle_t *trip;
    int ei;

    /* make space */
    if (tril >= 0 && (size_t)tril >= trin) {
	if (growtris(trin + 20) != 0)
		return -1;
    }
    trip = &tris[tril++];
    trip->mark = 0;
    trip->e[0].pnl0p = pnlap, trip->e[0].pnl1p = pnlbp, trip->e[0].rtp = NULL;
    trip->e[1].pnl0p = pnlbp, trip->e[1].pnl1p = pnlcp, trip->e[1].rtp = NULL;
    trip->e[2].pnl0p = pnlcp, trip->e[2].pnl1p = pnlap, trip->e[2].rtp = NULL;
    for (ei = 0; ei < 3; ei++)
	trip->e[ei].ltp = trip;

    return 0;
}

/* connect a pair of triangles at their common edge (if any) */
static void connecttris(long tri1, long tri2) {
    triangle_t *tri1p, *tri2p;
    int ei, ej;

    for (ei = 0; ei < 3; ei++) {
	for (ej = 0; ej < 3; ej++) {
	    tri1p = &tris[tri1];
	    tri2p = &tris[tri2];
	    if ((tri1p->e[ei].pnl0p->pp == tri2p->e[ej].pnl0p->pp &&
		 tri1p->e[ei].pnl1p->pp == tri2p->e[ej].pnl1p->pp) ||
		(tri1p->e[ei].pnl0p->pp == tri2p->e[ej].pnl1p->pp &&
		 tri1p->e[ei].pnl1p->pp == tri2p->e[ej].pnl0p->pp))
		tri1p->e[ei].rtp = tri2p, tri2p->e[ej].rtp = tri1p;
	}
    }
}

/* find and mark path from trii, to trij */
static bool marktripath(long trii, long trij) {
    int ei;

    if (tris[trii].mark)
	return false;
    tris[trii].mark = 1;
    if (trii == trij)
	return true;
    for (ei = 0; ei < 3; ei++)
	if (tris[trii].e[ei].rtp &&
	    marktripath(tris[trii].e[ei].rtp - tris, trij))
	    return true;
    tris[trii].mark = 0;
    return false;
}

/* add a new point to the deque, either front or back */
static void add2dq(int side, pointnlink_t * pnlp)
{
    if (side == DQ_FRONT) {
	if (dq.lpnlpi - dq.fpnlpi >= 0)
	    pnlp->link = dq.pnlps[dq.fpnlpi];	/* shortest path links */
	dq.fpnlpi--;
	dq.pnlps[dq.fpnlpi] = pnlp;
    } else {
	if (dq.lpnlpi - dq.fpnlpi >= 0)
	    pnlp->link = dq.pnlps[dq.lpnlpi];	/* shortest path links */
	dq.lpnlpi++;
	dq.pnlps[dq.lpnlpi] = pnlp;
    }
}

static void splitdq(int side, int index)
{
    if (side == DQ_FRONT)
	dq.lpnlpi = index;
    else
	dq.fpnlpi = index;
}

static int finddqsplit(pointnlink_t * pnlp)
{
    int index;

    for (index = dq.fpnlpi; index < dq.apex; index++)
	if (ccw(dq.pnlps[index + 1]->pp, dq.pnlps[index]->pp, pnlp->pp) == ISCCW)
	    return index;
    for (index = dq.lpnlpi; index > dq.apex; index--)
	if (ccw(dq.pnlps[index - 1]->pp, dq.pnlps[index]->pp, pnlp->pp) == ISCW)
	    return index;
    return dq.apex;
}

/* ccw test: CCW, CW, or co-linear */
static int ccw(Ppoint_t * p1p, Ppoint_t * p2p, Ppoint_t * p3p)
{
    double d;

    d = (p1p->y - p2p->y) * (p3p->x - p2p->x) -
	(p3p->y - p2p->y) * (p1p->x - p2p->x);
    return d > 0 ? ISCCW : (d < 0 ? ISCW : ISON);
}

/* line to line intersection */
static bool intersects(Ppoint_t * pap, Ppoint_t * pbp,
		      Ppoint_t * pcp, Ppoint_t * pdp)
{
    int ccw1, ccw2, ccw3, ccw4;

    if (ccw(pap, pbp, pcp) == ISON || ccw(pap, pbp, pdp) == ISON ||
	ccw(pcp, pdp, pap) == ISON || ccw(pcp, pdp, pbp) == ISON) {
	if (between(pap, pbp, pcp) || between(pap, pbp, pdp) ||
	    between(pcp, pdp, pap) || between(pcp, pdp, pbp))
	    return true;
    } else {
	ccw1 = ccw(pap, pbp, pcp) == ISCCW ? 1 : 0;
	ccw2 = ccw(pap, pbp, pdp) == ISCCW ? 1 : 0;
	ccw3 = ccw(pcp, pdp, pap) == ISCCW ? 1 : 0;
	ccw4 = ccw(pcp, pdp, pbp) == ISCCW ? 1 : 0;
	return (ccw1 ^ ccw2) && (ccw3 ^ ccw4);
    }
    return false;
}

/* is pbp between pap and pcp */
static bool between(Ppoint_t * pap, Ppoint_t * pbp, Ppoint_t * pcp)
{
    Ppoint_t p1, p2;

    p1.x = pbp->x - pap->x, p1.y = pbp->y - pap->y;
    p2.x = pcp->x - pap->x, p2.y = pcp->y - pap->y;
    if (ccw(pap, pbp, pcp) != ISON)
	return false;
    return p2.x * p1.x + p2.y * p1.y >= 0 &&
	p2.x * p2.x + p2.y * p2.y <= p1.x * p1.x + p1.y * p1.y;
}

static int pointintri(long trii, Ppoint_t *pp) {
    int ei, sum;

    for (ei = 0, sum = 0; ei < 3; ei++)
	if (ccw(tris[trii].e[ei].pnl0p->pp, tris[trii].e[ei].pnl1p->pp, pp) != ISCW)
	    sum++;
    return sum == 3 || sum == 0;
}

static int growpnls(size_t newpnln) {
    if (newpnln <= pnln)
	return 0;
    pnls = (pointnlink_t *)realloc(pnls, POINTNLINKSIZE * newpnln);
    if (pnls == NULL) {
	prerror("cannot realloc pnls");
	return -1;
    }
    pnlps = (pointnlink_t **) realloc(pnlps, POINTNLINKPSIZE * newpnln);
    if (pnlps == NULL) {
	prerror("cannot realloc pnlps");
	return -1;
    }
    pnln = newpnln;
    return 0;
}

static int growtris(size_t newtrin) {
    tris = (triangle_t *) realloc(tris, TRIANGLESIZE * newtrin);
    if (tris == NULL) {
	prerror("cannot realloc tris");
	return -1;
    }
    trin = newtrin;

    return 0;
}

static int growdq(int newdqn)
{
    if (newdqn <= dq.pnlpn)
	return 0;
    dq.pnlps = (pointnlink_t **) realloc(dq.pnlps, POINTNLINKPSIZE * newdqn);
    if (dq.pnlps == NULL) {
	prerror("cannot realloc dq.pnls");
	return -1;
    }
    dq.pnlpn = newdqn;
    return 0;
}

static int growops2(int newopn)
{
    if (newopn <= opn)
	return 0;
    ops = (Ppoint_t *)realloc(ops, POINTSIZE * newopn);
    if (ops == NULL) {
	prerror("cannot realloc ops");
	return -1;
    }
    opn = newopn;

    return 0;
}
