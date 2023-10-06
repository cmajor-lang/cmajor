/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "index.h"
#include "split.q.h"

/* Forward declarations */
static void MethodZero(RTree_t * rtp);
static void InitPVars(RTree_t * rtp);
static void LoadNodes(RTree_t * rtp, Node_t * n, Node_t * q,
		      struct PartitionVars *p);
static void Classify(RTree_t * rtp, int i, int group);
static void PickSeeds(RTree_t * rtp);
static void GetBranches(RTree_t * rtp, Node_t * n, Branch_t * b);

/*-----------------------------------------------------------------------------
| Split a node.
| Divides the nodes branches and the extra one between two nodes.
| Old node is one of the new ones, and one really new one is created.
| Tries more than one method for choosing a partition, uses best result.
-----------------------------------------------------------------------------*/
void SplitNode(RTree_t * rtp, Node_t * n, Branch_t * b, Node_t ** nn)
{
    assert(n);
    assert(b);

#ifdef RTDEBUG
    fprintf(stderr, "Splitting:\n");
    PrintNode(n);
    fprintf(stderr, "new branch:\n");
    PrintBranch(-1, b);
#endif

    /* load all the branches into a buffer, initialize old node */
    int level = n->level;
    GetBranches(rtp, n, b);

#ifdef RTDEBUG
    {
	/* Indicate that a split is about to take place */
	for (size_t i = 0; i < NODECARD + 1; i++) {
	    PrintRect(&rtp->split.BranchBuf[i].rect);
	}
	PrintRect(&rtp->split.CoverSplit);
    }
#endif

    /* find partition */
    struct PartitionVars *p = &rtp->split.Partitions[0];
    MethodZero(rtp);

    /* put branches from buffer into 2 nodes according to chosen partition */
    *nn = RTreeNewNode();
    (*nn)->level = n->level = level;
    LoadNodes(rtp, n, *nn, p);
    assert(n->count + (*nn)->count == NODECARD + 1);

#ifdef RTDEBUG
    PrintPVars(p);
    fprintf(stderr, "group 0:\n");
    PrintNode(n);
    fprintf(stderr, "group 1:\n");
    PrintNode(*nn);
    fprintf(stderr, "\n");
#endif

}

/*-----------------------------------------------------------------------------
| Load branch buffer with branches from full node plus the extra branch.
-----------------------------------------------------------------------------*/
static void GetBranches(RTree_t * rtp, Node_t * n, Branch_t * b)
{
    assert(n);
    assert(b);

    /* load the branch buffer */
    for (size_t i = 0; i < NODECARD; i++) {
	assert(n->branch[i].child);	/* node should have every entry full */
	rtp->split.BranchBuf[i] = n->branch[i];
    }
    rtp->split.BranchBuf[NODECARD] = *b;

    /* calculate rect containing all in the set */
    rtp->split.CoverSplit = rtp->split.BranchBuf[0].rect;
    for (size_t i = 1; i < NODECARD + 1; i++) {
	rtp->split.CoverSplit = CombineRect(&rtp->split.CoverSplit,
					    &rtp->split.BranchBuf[i].rect);
    }
    rtp->split.CoverSplitArea = RectArea(&rtp->split.CoverSplit);

    InitNode(n);
}

/*-----------------------------------------------------------------------------
| Method #0 for choosing a partition:
| As the seeds for the two groups, pick the two rects that would waste the
| most area if covered by a single rectangle, i.e. evidently the worst pair
| to have in the same group.
| Of the remaining, one at a time is chosen to be put in one of the two groups.
| The one chosen is the one with the greatest difference in area expansion
| depending on which group - the rect most strongly attracted to one group
| and repelled from the other.
| If one group gets too full (more would force other group to violate min
| fill requirement) then other group gets the rest.
| These last are the ones that can go in either group most easily.
-----------------------------------------------------------------------------*/
static void MethodZero(RTree_t * rtp)
{
    Rect_t *r;
    int growth0, growth1, diff, biggestDiff;
    int group, chosen = 0, betterGroup = 0;

    InitPVars(rtp);
    PickSeeds(rtp);

    while (rtp->split.Partitions[0].count[0] +
	   rtp->split.Partitions[0].count[1] < NODECARD + 1 &&
	   rtp->split.Partitions[0].count[0] < NODECARD + 1 - rtp->MinFill
	   && rtp->split.Partitions[0].count[1] <
	   NODECARD + 1 - rtp->MinFill) {
	biggestDiff = -1;
	for (int i = 0; i < NODECARD + 1; i++) {
	    if (!rtp->split.Partitions[0].taken[i]) {
		Rect_t rect;
		r = &rtp->split.BranchBuf[i].rect;
		rect = CombineRect(r, &rtp->split.Partitions[0].cover[0]);
		growth0 =
		    RectArea(&rect) - rtp->split.Partitions[0].area[0];
		rect = CombineRect(r, &rtp->split.Partitions[0].cover[1]);
		growth1 =
		    RectArea(&rect) - rtp->split.Partitions[0].area[1];
		diff = growth1 - growth0;
		if (diff >= 0)
		    group = 0;
		else {
		    group = 1;
		    diff = -diff;
		}

		if (diff > biggestDiff) {
		    biggestDiff = diff;
		    chosen = i;
		    betterGroup = group;
		} else if (diff == biggestDiff &&
			   rtp->split.Partitions[0].count[group] <
			   rtp->split.Partitions[0].count[betterGroup]) {
		    chosen = i;
		    betterGroup = group;
		}
	    }
	}
	Classify(rtp, chosen, betterGroup);
    }

    /* if one group too full, put remaining rects in the other */
    if (rtp->split.Partitions[0].count[0] +
	rtp->split.Partitions[0].count[1] < NODECARD + 1) {
	group = 0;
	if (rtp->split.Partitions[0].count[0] >=
	    NODECARD + 1 - rtp->MinFill)
	    group = 1;
	for (int i = 0; i < NODECARD + 1; i++) {
	    if (!rtp->split.Partitions[0].taken[i])
		Classify(rtp, i, group);
	}
    }

    assert(rtp->split.Partitions[0].count[0] +
	   rtp->split.Partitions[0].count[1] == NODECARD + 1);
    assert(rtp->split.Partitions[0].count[0] >= rtp->MinFill
	   && rtp->split.Partitions[0].count[1] >= rtp->MinFill);
}

/*-----------------------------------------------------------------------------
| Pick two rects from set to be the first elements of the two groups.
| Pick the two that waste the most area if covered by a single rectangle.
-----------------------------------------------------------------------------*/
static void PickSeeds(RTree_t * rtp)
{
  int seed0 = 0, seed1 = 0;
  unsigned int area[NODECARD + 1];

    for (int i = 0; i < NODECARD + 1; i++)
	area[i] = RectArea(&rtp->split.BranchBuf[i].rect);

    unsigned worst=0;
    for (int i = 0; i < NODECARD; i++) {
	for (int j = i + 1; j < NODECARD + 1; j++) {
	    Rect_t rect;
	    rect = CombineRect(&rtp->split.BranchBuf[i].rect,
			       &rtp->split.BranchBuf[j].rect);
	    unsigned waste = RectArea(&rect) - area[i] - area[j];
	    if (waste > worst) {
		worst = waste;
		seed0 = i;
		seed1 = j;
	    }
	}
    }
    Classify(rtp, seed0, 0);
    Classify(rtp, seed1, 1);
}

/*-----------------------------------------------------------------------------
| Put a branch in one of the groups.
-----------------------------------------------------------------------------*/
static void Classify(RTree_t * rtp, int i, int group)
{

    assert(!rtp->split.Partitions[0].taken[i]);

    rtp->split.Partitions[0].partition[i] = group;
    rtp->split.Partitions[0].taken[i] = true;

    if (rtp->split.Partitions[0].count[group] == 0)
	rtp->split.Partitions[0].cover[group] =
	    rtp->split.BranchBuf[i].rect;
    else
	rtp->split.Partitions[0].cover[group] =
	    CombineRect(&rtp->split.BranchBuf[i].rect,
			&rtp->split.Partitions[0].cover[group]);
    rtp->split.Partitions[0].area[group] =
	RectArea(&rtp->split.Partitions[0].cover[group]);
    rtp->split.Partitions[0].count[group]++;

#	ifdef RTDEBUG
    {
	/* redraw entire group and its cover */
	int j;
	MFBSetColor(WHITE);	/* cover is white */
	PrintRect(&rtp->split.Partitions[0].cover[group]);
	MFBSetColor(group + 3);	/* group 0 green, group 1 blue */
	for (j = 0; j < NODECARD + 1; j++) {
	    if (rtp->split.Partitions[0].taken[j] &&
		rtp->split.Partitions[0].partition[j] == group)
		PrintRect(&rtrtp->split.Partitions[0].BranchBuf[j].rect);
	}
	GraphChar();
    }
#	endif
}

/*-----------------------------------------------------------------------------
| Copy branches from the buffer into two nodes according to the partition.
-----------------------------------------------------------------------------*/
static void LoadNodes(RTree_t * rtp, Node_t * n, Node_t * q,
		      struct PartitionVars *p)
{
    assert(n);
    assert(q);
    assert(p);

    for (size_t i = 0; i < NODECARD + 1; i++) {
	assert(rtp->split.Partitions[0].partition[i] == 0 ||
	       rtp->split.Partitions[0].partition[i] == 1);
	if (rtp->split.Partitions[0].partition[i] == 0)
	    AddBranch(rtp, &rtp->split.BranchBuf[i], n, NULL);
	else if (rtp->split.Partitions[0].partition[i] == 1)
	    AddBranch(rtp, &rtp->split.BranchBuf[i], q, NULL);
    }
}

/*-----------------------------------------------------------------------------
| Initialize a PartitionVars structure.
-----------------------------------------------------------------------------*/
static void InitPVars(RTree_t * rtp)
{
    rtp->split.Partitions[0].count[0] = rtp->split.Partitions[0].count[1] =
	0;
    rtp->split.Partitions[0].cover[0] = rtp->split.Partitions[0].cover[1] =
	NullRect();
    rtp->split.Partitions[0].area[0] = rtp->split.Partitions[0].area[1] =
	0;
    for (size_t i = 0; i < NODECARD + 1; i++) {
	rtp->split.Partitions[0].taken[i] = false;
	rtp->split.Partitions[0].partition[i] = -1;
    }
}

#ifdef RTDEBUG

/*-----------------------------------------------------------------------------
| Print out data for a partition from PartitionVars struct.
-----------------------------------------------------------------------------*/
PrintPVars(RTree_t * rtp)
{
    fprintf(stderr, "\npartition:\n");
    for (size_t i = 0; i < NODECARD + 1; i++) {
	fprintf(stderr, "%3zu\t", i);
    }
    fprintf(stderr, "\n");
    for (size_t i = 0; i < NODECARD + 1; i++) {
	if (rtp->split.Partitions[0].taken[i])
	    fprintf(stderr, "  t\t");
	else
	    fprintf(stderr, "\t");
    }
    fprintf(stderr, "\n");
    for (size_t i = 0; i < NODECARD + 1; i++) {
	fprintf(stderr, "%3d\t", rtp->split.Partitions[0].partition[i]);
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "count[0] = %d  area = %d\n",
	    rtp->split.Partitions[0].count[0],
	    rtp->split.Partitions[0].area[0]);
    fprintf(stderr, "count[1] = %d  area = %d\n",
	    rtp->split.Partitions[0].count[1],
	    rtp->split.Partitions[0].area[1]);
    if (rtp->split.Partitions[0].area[0] +
	rtp->split.Partitions[0].area[1] > 0) {
	fprintf(stderr, "total area = %d  effectiveness = %3.2f\n",
		rtp->split.Partitions[0].area[0] +
		rtp->split.Partitions[0].area[1],
		(float) rtp->split.CoverSplitArea /
		(rtp->split.Partitions[0].area[0] +
		 rtp->split.Partitions[0].area[1]));
    }
    fprintf(stderr, "cover[0]:\n");
    PrintRect(&rtp->split.Partitions[0].cover[0]);

    fprintf(stderr, "cover[1]:\n");
    PrintRect(&rtp->split.Partitions[0].cover[1]);
}
#endif
