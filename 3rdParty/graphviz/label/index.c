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

LeafList_t *RTreeNewLeafList(Leaf_t * lp)
{
    LeafList_t *llp;

    if ((llp = (LeafList_t*)calloc(1, sizeof(LeafList_t)))) {
	llp->leaf = lp;
	llp->next = 0;
    }
    return llp;
}

LeafList_t *RTreeLeafListAdd(LeafList_t * llp, Leaf_t * lp)
{
    if (!lp)
	return llp;

    LeafList_t *nlp = RTreeNewLeafList(lp);
    nlp->next = llp;
    return nlp;
}

void RTreeLeafListFree(LeafList_t * llp)
{
    while (llp->next) {
	LeafList_t *tlp = llp->next;
	free(llp);
	llp = tlp;
    }
    free(llp);
    return;
}

RTree_t *RTreeOpen()
{
    RTree_t *rtp;

    if ((rtp = (RTree_t*)calloc(1, sizeof(RTree_t))))
	rtp->root = RTreeNewIndex();
    return rtp;
}

/* Make a new index, empty.  Consists of a single node. */
Node_t *RTreeNewIndex(void ) {
    Node_t *x = RTreeNewNode();
    x->level = 0;		/* leaf */
    return x;
}

static int RTreeClose2(RTree_t * rtp, Node_t * n)
{
    if (n->level > 0) {
	for (int i = 0; i < NODECARD; i++) {
	    if (!n->branch[i].child)
		continue;
	    if (!RTreeClose2(rtp, n->branch[i].child)) {
		free(n->branch[i].child);
		DisconBranch(n, i);
	    }
	}
    } else {
	for (int i = 0; i < NODECARD; i++) {
	    if (!n->branch[i].child)
		continue;
	    DisconBranch(n, i);
	}
    }
    return 0;
}


int RTreeClose(RTree_t * rtp)
{
    RTreeClose2(rtp, rtp->root);
    free(rtp->root);
    free(rtp);
    return 0;
}

#ifdef RTDEBUG
/* Print out all the nodes in an index.
** Prints from root downward.
*/
void PrintIndex(Node_t * n)
{
    Node_t *nn;
    assert(n);
    assert(n->level >= 0);

    if (n->level > 0) {
	for (size_t i = 0; i < NODECARD; i++) {
	    if ((nn = n->branch[i].child) != NULL)
		PrintIndex(nn);
	}
    }

    PrintNode(n);
}

/* Print out all the data rectangles in an index.
*/
void PrintData(Node_t * n)
{
    Node_t *nn;
    assert(n);
    assert(n->level >= 0);

    if (n->level == 0)
	PrintNode(n);
    else {
	for (size_t i = 0; i < NODECARD; i++) {
	    if ((nn = n->branch[i].child) != NULL)
		PrintData(nn);
	}
    }
}
#endif

/* RTreeSearch in an index tree or subtree for all data retangles that
** overlap the argument rectangle.
** Returns the number of qualifying data rects.
*/
LeafList_t *RTreeSearch(RTree_t * rtp, Node_t * n, Rect_t * r)
{
    LeafList_t *llp = 0;

    assert(n);
    assert(n->level >= 0);
    assert(r);

    if (n->level > 0) {		/* this is an internal node in the tree */
	for (size_t i = 0; i < NODECARD; i++)
	    if (n->branch[i].child && Overlap(r, &n->branch[i].rect)) {
		LeafList_t *tlp = RTreeSearch(rtp, n->branch[i].child, r);
		if (llp) {
		    LeafList_t *xlp = llp;
		    while (xlp->next)
			xlp = xlp->next;
		    xlp->next = tlp;
		} else
		    llp = tlp;
	    }
    } else {			/* this is a leaf node */
	for (size_t i = 0; i < NODECARD; i++) {
	    if (n->branch[i].child && Overlap(r, &n->branch[i].rect)) {
		llp = RTreeLeafListAdd(llp, (Leaf_t *) & n->branch[i]);
#				ifdef RTDEBUG
		PrintRect(&n->branch[i].rect);
#				endif
	    }
	}
    }
    return llp;
}

/* Insert a data rectangle into an index structure.
** RTreeInsert provides for splitting the root;
** returns 1 if root was split, 0 if it was not.
** The level argument specifies the number of steps up from the leaf
** level to insert; e.g. a data rectangle goes in at level = 0.
** RTreeInsert2 does the recursion.
*/
static int RTreeInsert2(RTree_t *, Rect_t *, void *, Node_t *, Node_t **, int);

int RTreeInsert(RTree_t * rtp, Rect_t * r, void *data, Node_t ** n, int level)
{
    Node_t *newnode=0;
    Branch_t b;
    int result = 0;


    assert(r && n);
    assert(level >= 0 && level <= (*n)->level);
    for (size_t i = 0; i < NUMDIMS; i++)
	assert(r->boundary[i] <= r->boundary[NUMDIMS + i]);

#	ifdef RTDEBUG
    fprintf(stderr, "RTreeInsert  level=%d\n", level);
#	endif

    if (RTreeInsert2(rtp, r, data, *n, &newnode, level)) {	/* root was split */

	Node_t *newroot = RTreeNewNode();	/* grow a new root, make tree taller */
	newroot->level = (*n)->level + 1;
	b.rect = NodeCover(*n);
	b.child = *n;
	AddBranch(rtp, &b, newroot, NULL);
	b.rect = NodeCover(newnode);
	b.child = newnode;
	AddBranch(rtp, &b, newroot, NULL);
	*n = newroot;
	result = 1;
    }

    return result;
}

/* Inserts a new data rectangle into the index structure.
** Recursively descends tree, propagates splits back up.
** Returns 0 if node was not split.  Old node updated.
** If node was split, returns 1 and sets the pointer pointed to by
** new to point to the new node.  Old node updated to become one of two.
** The level argument specifies the number of steps up from the leaf
** level to insert; e.g. a data rectangle goes in at level = 0.
*/
static int
RTreeInsert2(RTree_t * rtp, Rect_t * r, void *data,
	     Node_t * n, Node_t ** newData, int level)
{
    Branch_t b;
    Node_t *n2=0;

    assert(r && n && newData);
    assert(level >= 0 && level <= n->level);

    /* Still above level for insertion, go down tree recursively */
    if (n->level > level) {
	int i = PickBranch(r, n);
	if (!RTreeInsert2(rtp, r, data, n->branch[i].child, &n2, level)) {	/* recurse: child was not split */
	    n->branch[i].rect = CombineRect(r, &(n->branch[i].rect));
	    return 0;
	} else {		/* child was split */
	    n->branch[i].rect = NodeCover(n->branch[i].child);
	    b.child = n2;
	    b.rect = NodeCover(n2);
	    return AddBranch(rtp, &b, n, newData);
	}
    } else if (n->level == level) {	/* at level for insertion. */
	/*Add rect, split if necessary */
	b.rect = *r;
	b.child = (Node_t *) data;
	return AddBranch(rtp, &b, n, newData);
    } else {			/* Not supposed to happen */
	assert(false);
	return 0;
    }
}
