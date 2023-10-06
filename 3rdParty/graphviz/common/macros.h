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

#define isPinned(n)     (ND_pinned(n) == P_PIN)
#define hasPos(n)       (ND_pinned(n) > 0)
#define isFixed(n)      (ND_pinned(n) > P_SET)

#define CL_EDGE_TAG "cl_edge_info"
#define SET_CLUST_NODE(n) (ND_clustnode(n) = true)
#define IS_CLUST_NODE(n)  (ND_clustnode(n))
#define HAS_CLUST_EDGE(g) (aggetrec(g, CL_EDGE_TAG, FALSE))
#define EDGE_TYPE(g) (GD_flags(g) & (7 << 1))

#ifndef streq
#define streq(a,b)		(!strcmp(a,b))
#endif

#define XPAD(d) ((d).x += 4*GAP)
#define YPAD(d) ((d).y += 2*GAP)
#define PAD(d)  {XPAD(d); YPAD(d);}
