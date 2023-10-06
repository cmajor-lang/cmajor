
#include	"dthdr.h"
#include	<stddef.h>

/*	Change search method.
**
**	Written by Kiem-Phong Vo (05/25/96)
*/

Dtmethod_t* dtmethod(Dt_t* dt, Dtmethod_t* meth)
{
	Dtlink_t	*list, *r;
	Dtdisc_t*	disc = dt->disc;
	Dtmethod_t*	oldmeth = dt->meth;

	if(!meth || meth->type == oldmeth->type)
		return oldmeth;

	if (disc->eventf && disc->eventf(dt, DT_METH, meth, disc) < 0)
		return NULL;

	dt->data->minp = 0;

	/* get the list of elements */
	list = dtflatten(dt);

	if(dt->data->type&(DT_LIST|DT_STACK|DT_QUEUE) )
		dt->data->head = NULL;
	else if(dt->data->type&(DT_SET|DT_BAG) )
	{	if(dt->data->ntab > 0)
			dt->memoryf(dt, dt->data->htab, 0, disc);
		dt->data->ntab = 0;
		dt->data->htab = NULL;
	}

	dt->data->here = NULL;
	dt->data->type = (dt->data->type&~(DT_METHODS|DT_FLATTEN)) | meth->type;
	dt->meth = meth;
	if(dt->searchf == oldmeth->searchf)
		dt->searchf = meth->searchf;

	if(meth->type&(DT_LIST|DT_STACK|DT_QUEUE) )
	{	if(!(oldmeth->type&(DT_LIST|DT_STACK|DT_QUEUE)) )
		{	if((r = list) )
			{	Dtlink_t*	t;
				for(t = r->right; t; r = t, t = t->right )
					t->left = r;
				list->left = r;
			}
		}
		dt->data->head = list;
	}
	else if(meth->type&(DT_OSET|DT_OBAG))
	{	dt->data->size = 0;
		while(list)
		{	r = list->right;
			meth->searchf(dt, list, DT_RENEW);
			list = r;
		}
	}
	else if(!((meth->type&DT_BAG) && (oldmeth->type&DT_SET)) )
	{	int	rehash;
		if((meth->type&(DT_SET|DT_BAG)) && !(oldmeth->type&(DT_SET|DT_BAG)))
			rehash = 1;
		else	rehash = 0;

		dt->data->size = dt->data->loop = 0;
		while(list)
		{	r = list->right;
			if(rehash)
			{	void* key = _DTOBJ(list,disc->link);
				key = _DTKEY(key,disc->key,disc->size);
				list->hash = _DTHSH(dt,key,disc,disc->size);
			}
			(void)meth->searchf(dt, list, DT_RENEW);
			list = r;
		}
	}

	return oldmeth;
}
