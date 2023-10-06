/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

/*
 * in_poly
 *
 * Test if a point is inside a polygon.
 * The polygon must be convex with vertices in CW order.
 */

#include "vispath.h"
#include "pathutil.h"

int in_poly(Ppoly_t poly, Ppoint_t q)
{
    int i, i1;			/* point index; i1 = i-1 mod n */
    int n;
    Ppoint_t *P;

    P = poly.ps;
    n = poly.pn;
    for (i = 0; i < n; i++) {
	i1 = (i + n - 1) % n;
	if (wind(P[i1],P[i],q) == 1) return FALSE;
    }
    return TRUE;
}
