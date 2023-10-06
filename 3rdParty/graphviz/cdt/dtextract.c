#include	"dthdr.h"
#include	<stddef.h>

/*	Extract objects of a dictionary.
**
**	Written by Kiem-Phong Vo (5/25/96).
*/

Dtlink_t* dtextract(Dt_t* dt)
{
	Dtlink_t	*list, **s, **ends;

	if(dt->data->type&(DT_OSET|DT_OBAG) )
		list = dt->data->here;
	else if(dt->data->type&(DT_SET|DT_BAG))
	{	list = dtflatten(dt);
		for(ends = (s = dt->data->htab) + dt->data->ntab; s < ends; ++s)
			*s = NULL;
	}
	else /*if(dt->data->type&(DT_LIST|DT_STACK|DT_QUEUE))*/
	{	list = dt->data->head;
		dt->data->head = NULL;
	}

	dt->data->type &= ~DT_FLATTEN;
	dt->data->size = 0;
	dt->data->here = NULL;

	return list;
}
