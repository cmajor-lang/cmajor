#include	"dthdr.h"
#include	<stddef.h>

/* 	Make a new dictionary
**
**	Written by Kiem-Phong Vo (5/25/96)
*/

Dt_t* dtopen(Dtdisc_t* disc, Dtmethod_t* meth)
{
	Dt_t*		dt;
	int		e;
	Dtdata_t*	data;

	if(!disc || !meth)
		return NULL;

	/* allocate space for dictionary */
	if(!(dt = (Dt_t*)malloc(sizeof(Dt_t))))
		return NULL;

	/* initialize all absolutely private data */
	dt->searchf = NULL;
	dt->meth = NULL;
	dt->disc = NULL;
	dtdisc(dt,disc,0);
	dt->type = DT_MALLOC;
	dt->nview = 0;
	dt->view = dt->walk = NULL;
	dt->user = NULL;

	if(disc->eventf)
	{	/* if shared/persistent dictionary, get existing data */
		data = NULL;
		if ((e = disc->eventf(dt, DT_OPEN, &data, disc)) < 0)
			goto err_open;
		else if(e > 0)
		{	if(data)
			{	if(data->type&meth->type)
					goto done;
				else	goto err_open;
			}

			if(!disc->memoryf)
				goto err_open;

			free(dt);
			if (!(dt = (Dt_t*) disc->memoryf(0, 0, sizeof(Dt_t), disc)))
				return NULL;
			dt->searchf = NULL;
			dt->meth = NULL;
			dt->disc = NULL;
			dtdisc(dt,disc,0);
			dt->type = DT_MEMORYF;
			dt->nview = 0;
			dt->view = dt->walk = NULL;
		}
	}

	/* allocate sharable data */
	if (!(data = (Dtdata_t*) dt->memoryf(dt, NULL, sizeof(Dtdata_t), disc)))
	{ err_open:
		free(dt);
		return NULL;
	}

	data->type = meth->type;
	data->here = NULL;
	data->htab = NULL;
	data->ntab = data->size = data->loop = 0;
	data->minp = 0;

done:
	dt->data = data;
	dt->searchf = meth->searchf;
	dt->meth = meth;

	if(disc->eventf)
		(*disc->eventf)(dt, DT_ENDOPEN, dt, disc);

	return dt;
}
