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
#include "../cgraph/bitarray.h"
#include "dot.h"

/*
 * Author: Mohammad T. Irfan
 *   Summer, 2008
 */

/* TODO:
 *   - Support clusters
 *   - Support disconnected graphs
 *   - Provide algorithms for aspect ratios < 1
 */

#define MIN_AR 1.0
#define MAX_AR 20.0
#define DEF_PASSES 5
#define DPI 72

/*
 *       NODE GROUPS FOR SAME RANKING
 *       Node group data structure groups nodes together for
 *       MIN, MAX, SOURCE, SINK constraints.
 *       The grouping is based on the union-find data structure and
 *       provides sequential access to the nodes in the same group.
 */

/* data structure for node groups */
typedef struct nodeGroup_t {
    node_t **nodes;
    int nNodes;
    double width, height;
} nodeGroup_t;

static nodeGroup_t *nodeGroups;
static int nNodeGroups = 0;

/* computeNodeGroups:
 * computeNodeGroups function does the groupings of nodes.
 * The grouping is based on the union-find data structure.
 */
static void computeNodeGroups(graph_t * g)
{
    node_t *n;

    nodeGroups = (nodeGroup_t*)gv_calloc(agnnodes(g), sizeof(nodeGroup_t));

    nNodeGroups = 0;

    /* initialize node ids. Id of a node is used as an index to the group. */
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	ND_id(n) = -1;
    }

    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (ND_UF_size(n) == 0) {	/* no same ranking constraint */
	    nodeGroups[nNodeGroups].nodes = (node_t**) gv_alloc(sizeof(node_t*));
	    nodeGroups[nNodeGroups].nodes[0] = n;
	    nodeGroups[nNodeGroups].nNodes = 1;
	    nodeGroups[nNodeGroups].width = ND_width(n);
	    nodeGroups[nNodeGroups].height = ND_height(n);

	    ND_id(n) = nNodeGroups;
	    nNodeGroups++;
	} else			/* group same ranked nodes */
	{
	    node_t *l = UF_find(n);
	    if (ND_id(l) > -1)	/* leader is already grouped */
	    {
		int index = ND_id(l);
		nodeGroups[index].nodes[nodeGroups[index].nNodes++] = n;
		nodeGroups[index].width += ND_width(n);
		nodeGroups[index].height =
		    (nodeGroups[index].height <
		     ND_height(n)) ? ND_height(n) : nodeGroups[index].
		    height;

		ND_id(n) = index;
	    } else		/* create a new group */
	    {
		nodeGroups[nNodeGroups].nodes = (node_t**) gv_calloc(ND_UF_size(l), sizeof(node_t*));

		if (l == n)	/* node n is the leader */
		{
		    nodeGroups[nNodeGroups].nodes[0] = l;
		    nodeGroups[nNodeGroups].nNodes = 1;
		    nodeGroups[nNodeGroups].width = ND_width(l);
		    nodeGroups[nNodeGroups].height = ND_height(l);
		} else {
		    nodeGroups[nNodeGroups].nodes[0] = l;
		    nodeGroups[nNodeGroups].nodes[1] = n;
		    nodeGroups[nNodeGroups].nNodes = 2;
		    nodeGroups[nNodeGroups].width =
			ND_width(l) + ND_width(n);
		    nodeGroups[nNodeGroups].height =
			(ND_height(l) <
			 ND_height(n)) ? ND_height(n) : ND_height(l);
		}

		ND_id(l) = nNodeGroups;
		ND_id(n) = nNodeGroups;
		nNodeGroups++;
	    }
	}
    }

}

/*
 *       END OF CODES FOR NODE GROUPS
 */

/* countDummyNodes:
 *  Count the number of dummy nodes
 */
int countDummyNodes(graph_t * g)
{
    int count = 0;
    node_t *n;
    edge_t *e;

    /* Count dummy nodes */
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
		/* flat edges do not have dummy nodes */
	    if (ND_rank(aghead(e)) != ND_rank(agtail(e)))
		count += abs(ND_rank(aghead(e)) - ND_rank(agtail(e))) - 1;
	}
    }
    return count;
}

/*
 *       FFDH PACKING ALGORITHM TO ACHIEVE TARGET ASPECT RATIO
 */

/*
 *  layerWidthInfo_t: data structure for keeping layer width information
 *  Each layer consists of a number of node groups.
 */
typedef struct layerWidthInfo_t {
    int layerNumber;
    nodeGroup_t **nodeGroupsInLayer;
    bitarray_t removed; // is the node group removed?
    int nNodeGroupsInLayer;
    int nDummyNodes;
    double width;
    double height;
} layerWidthInfo_t;

static layerWidthInfo_t *layerWidthInfo = NULL;
static int *sortedLayerIndex;
static int nLayers = 0;

/* computeLayerWidths:
 */
static void computeLayerWidths(graph_t * g)
{
    int i;
    node_t *v;
    node_t *n;
    edge_t *e;

    nLayers = 0;

    /* free previously allocated memory */
    if (layerWidthInfo) {
	for (i = 0; i < nNodeGroups; i++) {
	    if (layerWidthInfo[i].nodeGroupsInLayer) {
		int j;
		for (j = 0; j < layerWidthInfo[i].nNodeGroupsInLayer; j++) {
		    //if (layerWidthInfo[i].nodeGroupsInLayer[j])
		    //free(layerWidthInfo[i].nodeGroupsInLayer[j]);
		}
		free(layerWidthInfo[i].nodeGroupsInLayer);
	    }
	    bitarray_reset(&layerWidthInfo[i].removed);
	}

	free(layerWidthInfo);
    }
    /* allocate memory
     * the number of layers can go up to the number of node groups
     */
    layerWidthInfo = (layerWidthInfo_t*) gv_calloc(nNodeGroups, sizeof(layerWidthInfo_t));

    for (i = 0; i < nNodeGroups; i++) {
	layerWidthInfo[i].nodeGroupsInLayer = (nodeGroup_t**)gv_calloc(nNodeGroups, sizeof(nodeGroup_t*));

	assert(nNodeGroups >= 0);
	layerWidthInfo[i].removed = bitarray_new((size_t)nNodeGroups);

	layerWidthInfo[i].layerNumber = i;
	layerWidthInfo[i].nNodeGroupsInLayer = 0;
	layerWidthInfo[i].nDummyNodes = 0;
	layerWidthInfo[i].width = 0.0;
	layerWidthInfo[i].height = 0.0;
    }



    /* Count dummy nodes in the layer */
    for (n = agfstnode(g); n; n = agnxtnode(g, n))
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
	    int k;

	    /* FIX: This loop maybe unnecessary, but removing it and using
             * the commented codes next, gives a segmentation fault. I
             * forgot the reason why.
             */
	    for (k = ND_rank(agtail(e)) + 1; k < ND_rank(aghead(e)); k++) {
		layerWidthInfo[k].nDummyNodes++;
	    }

	}

    /* gather the layer information */
    for (i = 0; i < nNodeGroups; i++) {
	v = nodeGroups[i].nodes[0];
	if (ND_rank(v) + 1 > nLayers)	/* update the number of layers */
	    nLayers = ND_rank(v) + 1;

	layerWidthInfo[ND_rank(v)].width +=
	    nodeGroups[i].width * DPI + (layerWidthInfo[ND_rank(v)].width >
					 0) * GD_nodesep(g);
	if (layerWidthInfo[ND_rank(v)].height < nodeGroups[i].height * DPI)
	    layerWidthInfo[ND_rank(v)].height = nodeGroups[i].height * DPI;
	layerWidthInfo[ND_rank(v)].
	    nodeGroupsInLayer[layerWidthInfo[ND_rank(v)].
			      nNodeGroupsInLayer] = &nodeGroups[i];
	layerWidthInfo[ND_rank(v)].nNodeGroupsInLayer++;
    }

}

/* compFunction:
 * Comparison function to be used in qsort.
 * For sorting the layers by nonincreasing width
 */
static int compFunction(const void *a, const void *b)
{
    const int *ind1 = (const int *) a;
    const int *ind2 = (const int *) b;

    return (layerWidthInfo[*ind2].width >
	    layerWidthInfo[*ind1].width) - (layerWidthInfo[*ind2].width <
					    layerWidthInfo[*ind1].width);
}

/* sortLayers:
 * Sort the layers by width (nonincreasing order)
 * qsort should be replaced by insertion sort for better performance.
 * (layers are "almost" sorted during iterations)
 */
static void sortLayers(graph_t * g)
{
    qsort(sortedLayerIndex, agnnodes(g), sizeof(int), compFunction);
}

/* getOutDegree:
 * Return the sum of out degrees of the nodes in a node group.
 */
static int getOutDegree(nodeGroup_t * ng)
{
    int i, cnt = 0;
    for (i = 0; i < ng->nNodes; i++) {
	node_t *n = ng->nodes[i];
	edge_t *e;
	graph_t *g = agraphof(n);

	/* count outdegree. This loop might be unnecessary. */
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
	    cnt++;
	}
    }

    return cnt;
}

/* compFunction2:
 * Comparison function to be used in qsort.
 * For sorting the node groups by their out degrees (nondecreasing)
 */
static int compFunction2(const void *a, const void *b)
{
    nodeGroup_t **ind1 = (nodeGroup_t **) a, **ind2 = (nodeGroup_t **) b;

    int cnt1 = getOutDegree(*ind1);
    int cnt2 = getOutDegree(*ind2);

    return (cnt2 < cnt1) - (cnt2 > cnt1);
}

/* reduceMaxWidth2:
 * This is the main heuristic for partitioning the widest layer.
 * Partitioning is based on outdegrees of nodes.
 * Replace compFunction2 by compFunction3 if you want to partition
 * by node widths and heights.
 * FFDH procedure
 */
static void reduceMaxWidth2(graph_t * g)
{
    int i;
    int maxLayerIndex = 0;
    double nextMaxWidth = 0.0;
    double w = 0;
    double targetWidth;
    int fst;
    nodeGroup_t *fstNdGrp = NULL;
    int ndem;
    int p, q;
    int limit;
    int rem;
    int rem2;


    /* Find the widest layer. it must have at least 2 nodes. */
    for (i = 0; i < nLayers; i++) {
	if (layerWidthInfo[sortedLayerIndex[i]].nNodeGroupsInLayer <= 1)
	    continue;
	else {
	    maxLayerIndex = sortedLayerIndex[i];
	    /* get the width of the next widest layer */
	    nextMaxWidth =
		(nLayers >
		 i + 1) ? layerWidthInfo[sortedLayerIndex[i +
							  1]].width : 0;
	    break;
	}
    }

    if (i == nLayers)
	return;			/* reduction of layerwidth is not possible. */

    /* sort the node groups in maxLayerIndex layer by height and
     * then width, nonincreasing
     */
    qsort(layerWidthInfo[maxLayerIndex].nodeGroupsInLayer,
	  layerWidthInfo[maxLayerIndex].nNodeGroupsInLayer,
	  sizeof(nodeGroup_t *), compFunction2);

    if (nextMaxWidth <= layerWidthInfo[maxLayerIndex].width / 4
	|| nextMaxWidth >= layerWidthInfo[maxLayerIndex].width * 3 / 4)
	nextMaxWidth = layerWidthInfo[maxLayerIndex].width / 2;

    targetWidth = nextMaxWidth;	/* layerWidthInfo[maxLayerIndex].width/2; */

    /* now partition the current layer into two or more
     * layers (determined by the ranking algorithm)
     */
    fst = 0;
    ndem = 0;
    limit = layerWidthInfo[maxLayerIndex].nNodeGroupsInLayer;
    rem = 0;
    rem2 = 0;

    /* initialize w, the width of the widest layer after partitioning */
    w = 0;

    for (i = 0; i < limit + rem; i++) {
	if (bitarray_get(layerWidthInfo[maxLayerIndex].removed, i)) {
	    rem++;
	    continue;
	}

	if ((w +
	     layerWidthInfo[maxLayerIndex].nodeGroupsInLayer[i]->width *
	     DPI + (w > 0) * GD_nodesep(g) <= targetWidth)
	    || !fst) {
	    w += (layerWidthInfo[maxLayerIndex].nodeGroupsInLayer[i])->
		width * DPI + (w > 0) * GD_nodesep(g);
	    if (!fst) {
		fstNdGrp =
		    layerWidthInfo[maxLayerIndex].nodeGroupsInLayer[i];
		fst = 1;
	    }
	} else {
	    nodeGroup_t *ng =
		layerWidthInfo[maxLayerIndex].nodeGroupsInLayer[i];


	    for (p = 0; p < fstNdGrp->nNodes; p++)
		for (q = 0; q < ng->nNodes; q++) {
		    //printf("Trying to add virtual edge: %s -> %s\n",
		    //    agnameof(fstNdGrp->nodes[p]), agnameof(ng->nodes[q]));

		    /* The following code is for deletion of long virtual edges.
		     * It's no longer used.
		     */

		    /* add a new virtual edge */
		    edge_t *newVEdge =
			virtual_edge(fstNdGrp->nodes[p], ng->nodes[q],
				     NULL);
		    ED_edge_type(newVEdge) = VIRTUAL;
		    ndem++;	/* increase number of node demotions */
		}

	    /* the following code updates the layer width information. The
	     * update is not useful in the current version of the heuristic.
	     */
	    bitarray_set(&layerWidthInfo[maxLayerIndex].removed, i, true);
	    rem2++;
	    layerWidthInfo[maxLayerIndex].nNodeGroupsInLayer--;
	    /* SHOULD BE INCREASED BY THE SUM OF INDEG OF ALL NODES IN GROUP */
	    layerWidthInfo[maxLayerIndex].nDummyNodes++;
	    layerWidthInfo[maxLayerIndex].width -=
		(ng->width * DPI + GD_nodesep(g));
	}
    }
}

/* applyPacking2:
 * The following is the packing heuristic for wide layout.
 */
static void applyPacking2(graph_t * g)
{
    int i;

    sortedLayerIndex = (int*) gv_calloc(agnnodes(g), sizeof(int));

    for (i = 0; i < agnnodes(g); i++) {
	sortedLayerIndex[i] = i;
    }

    computeLayerWidths(g);
    sortLayers(g);
    reduceMaxWidth2(g);

}

/****************************************************************
 * Initialize all the edge types to NORMAL
 ****************************************************************/
void initEdgeTypes(graph_t * g)
{
    edge_t *e;
    node_t *n;
    int lc;

    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	for (lc = 0; lc < ND_in(n).size; lc++) {
	    e = ND_in(n).list[lc];
	    ED_edge_type(e) = NORMAL;
	}
    }
}

/* computeCombiAR:
 * Compute and return combinatorial aspect ratio
 * =
 * Width of the widest layer / Height
 * (in ranking phase)
 */
static double computeCombiAR(graph_t * g)
{
    int i;
    double maxW = 0;
    double maxH;
    double ratio;

    computeLayerWidths(g);
    maxH = (nLayers - 1) * GD_ranksep(g);

    for (i = 0; i < nLayers; i++) {
	if (maxW <
	    layerWidthInfo[i].width +
	    layerWidthInfo[i].nDummyNodes * GD_nodesep(g)) {
	    maxW =
		layerWidthInfo[i].width +
		layerWidthInfo[i].nDummyNodes * GD_nodesep(g);
	}
	maxH += layerWidthInfo[i].height;
    }

    ratio = maxW / maxH;

    return ratio;
}

/* zapLayers:
 * After applying the expansion heuristic, some layers are
 * found to be empty.
 * This function removes the empty layers.
 */
static void zapLayers(void)
{
    int i, j;
    int start = 0;
    int count = 0;

    /* the layers are sorted by the layer number.  now zap the empty layers */

    for (i = 0; i < nLayers; i++) {
	if (layerWidthInfo[i].nNodeGroupsInLayer == 0) {
	    if (count == 0)
		start = layerWidthInfo[i].layerNumber;
	    count++;
	} else if (count && layerWidthInfo[i].layerNumber > start) {
	    for (j = 0; j < layerWidthInfo[i].nNodeGroupsInLayer; j++) {
		int q;
		nodeGroup_t *ng = layerWidthInfo[i].nodeGroupsInLayer[j];
		for (q = 0; q < ng->nNodes; q++) {
		    node_t *nd = ng->nodes[q];
		    ND_rank(nd) -= count;
		}
	    }
	}
    }
}

/* rank3:
 * ranking function for dealing with wide/narrow graphs,
 * or graphs with varying node widths and heights.
 * This function iteratively calls dot's rank1() function and
 * applies packing (by calling the applyPacking2 function.
 * applyPacking2 function calls the reduceMaxWidth2 function
 * for partitioning the widest layer).
 * Initially the iterations argument is -1, for which rank3
 * callse applyPacking2 function until the combinatorial aspect
 * ratio is <= the desired aspect ratio.
 */
void rank3(graph_t * g, aspect_t * asp)
{
    Agnode_t *n;
    int i;
    int iterations = asp->nextIter;
    double lastAR = MAXDOUBLE;

    computeNodeGroups(g);	/* groups of UF DS nodes */

    for (i = 0; (i < iterations) || (iterations == -1); i++) {
	/* initialize all ranks to be 0 */
	for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	    ND_rank(n) = 0;
	}

	/* need to compute ranking first--- by Network flow */

	rank1(g);

	asp->combiAR = computeCombiAR(g);
	if (Verbose)
	    fprintf(stderr, "combiAR = %lf\n", asp->combiAR);

        /* Success or if no improvement */
	if ((asp->combiAR <= asp->targetAR) || ((iterations == -1) && (lastAR <= asp->combiAR))) {
	    asp->prevIterations = asp->curIterations;
	    asp->curIterations = i;

	    break;
	}
	lastAR = asp->combiAR;
	/* Apply the FFDH algorithm to reduce the aspect ratio; */
	applyPacking2(g);
    }

    /* do network flow once again... incorporating the added edges */
    rank1(g);

    computeLayerWidths(g);
    zapLayers();
    asp->combiAR = computeCombiAR(g);
}

/* init_UF_size:
 * Initialize the Union Find data structure
 */
void init_UF_size(graph_t * g)
{
    node_t *n;

    for (n = agfstnode(g); n; n = agnxtnode(g, n))
	ND_UF_size(n) = 0;
}

aspect_t*
setAspect (Agraph_t * g, aspect_t* adata)
{
    double rv;
    char *p;
    int r, passes = DEF_PASSES;

    p = agget (g, "aspect");

    if (!p || ((r = sscanf (p, "%lf,%d", &rv, &passes)) <= 0)) {
	adata->nextIter = 0;
	adata->badGraph = 0;
	adata->nPasses = 0;
	return NULL;
    }
    agerr (AGWARN, "the aspect attribute has been disabled due to implementation flaws - attribute ignored.\n");
    adata->nextIter = 0;
    adata->badGraph = 0;
    adata->nPasses = 0;
    return NULL;

    if (rv < MIN_AR) rv = MIN_AR;
    else if (rv > MAX_AR) rv = MAX_AR;
    adata->targetAR = rv;
    adata->nextIter = -1;
    adata->nPasses = passes;
    adata->badGraph = 0;

    if (Verbose)
        fprintf(stderr, "Target AR = %g\n", adata->targetAR);

    return adata;
}
