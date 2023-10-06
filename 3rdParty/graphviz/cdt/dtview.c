
#include	"dthdr.h"
#include	<stddef.h>

/*	Set a view path from dict to view.
**
**	Written by Kiem-Phong Vo (5/25/96)
*/


static void* dtvsearch(Dt_t* dt, void* obj, int type)
{
	Dt_t		*d, *p;
	void *o = NULL, *n, *ok, *nk;
	int		cmp, lk, sz, ky;
	Dtcompar_f	cmpf;
	(void)lk;

	/* these operations only happen at the top level */
	if(type&(DT_INSERT|DT_DELETE|DT_CLEAR|DT_RENEW))
		return (*(dt->meth->searchf))(dt,obj,type);

	if((type&(DT_MATCH|DT_SEARCH)) || /* order sets first/last done below */
	   ((type&(DT_FIRST|DT_LAST)) && !(dt->meth->type&(DT_OBAG|DT_OSET)) ) )
	{	for(d = dt; d; d = d->view)
			if ((o = d->meth->searchf(d, obj, type)))
				break;
		dt->walk = d;
		return o;
	}

	if(dt->meth->type & (DT_OBAG|DT_OSET) )
	{	if(!(type & (DT_FIRST|DT_LAST|DT_NEXT|DT_PREV)) )
			return NULL;

		n = nk = NULL; p = NULL;
		for(d = dt; d; d = d->view)
		{	if (!(o = d->meth->searchf(d, obj, type)))
				continue;
			_DTDSC(d->disc,ky,sz,lk,cmpf);
			ok = _DTKEY(o,ky,sz);

			if(n) /* get the right one among all dictionaries */
			{	cmp = _DTCMP(d,ok,nk,d->disc,cmpf,sz);
				if(((type & (DT_NEXT|DT_FIRST)) && cmp < 0) ||
				   ((type & (DT_PREV|DT_LAST)) && cmp > 0) )
					goto a_dj;
			}
			else /* looks good for now */
			{ a_dj: p  = d;
				n  = o;
				nk = ok;
			}
		}

		dt->walk = p;
		return n;
	}

	/* non-ordered methods */
	if(!(type & (DT_NEXT|DT_PREV)) )
		return NULL;

	if(!dt->walk || obj != _DTOBJ(dt->walk->data->here, dt->walk->disc->link) )
	{	for(d = dt; d; d = d->view)
			if ((o = d->meth->searchf(d, obj, DT_SEARCH)))
				break;
		dt->walk = d;
		if(!(obj = o) )
			return NULL;
	}

	for (d = dt->walk, obj = d->meth->searchf(d, obj, type);; )
	{	while(obj) /* keep moving until finding an uncovered object */
		{	for(p = dt; ; p = p->view)
			{	if(p == d) /* adjacent object is uncovered */
					return obj;
				if (p->meth->searchf(p, obj, DT_SEARCH))
					break;
			}
			obj = d->meth->searchf(d, obj, type);
		}

		if(!(d = dt->walk = d->view) ) /* move on to next dictionary */
			return NULL;
		else if(type&DT_NEXT)
			obj = d->meth->searchf(d, NULL, DT_FIRST);
		else	obj = d->meth->searchf(d, NULL, DT_LAST);
	}
}

Dt_t* dtview(Dt_t* dt, Dt_t* view)
{
	Dt_t*	d;

	UNFLATTEN(dt);
	if(view)
	{	UNFLATTEN(view);
		if(view->meth != dt->meth) /* must use the same method */
			return NULL;
	}

	/* make sure there won't be a cycle */
	for(d = view; d; d = d->view)
		if(d == dt)
			return NULL;

	/* no more viewing lower dictionary */
	if((d = dt->view) )
		d->nview -= 1;
	dt->view = dt->walk = NULL;

	if(!view)
	{	dt->searchf = dt->meth->searchf;
		return d;
	}

	/* ok */
	dt->view = view;
	dt->searchf = dtvsearch;
	view->nview += 1;

	return view;
}
