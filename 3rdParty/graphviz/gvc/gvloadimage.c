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
 *  graphics code generator wrapper
 *
 *  This library forms the socket for run-time loadable loadimage plugins.
 */

#include "config.h"

#include "../common/const.h"
#include "../gvc/gvplugin_loadimage.h"
#include "../gvc/gvcint.h"
#include "../gvc/gvcproc.h"

/* for agerr() */
#include "../cgraph/cgraph.h"

static int gvloadimage_select(GVJ_t * job, char *str)
{
    gvplugin_available_t *plugin;
    gvplugin_installed_t *typeptr;

    plugin = gvplugin_load(job->gvc, API_loadimage, str);
    if (plugin) {
        typeptr = plugin->typeptr;
        job->loadimage.engine = (gvloadimage_engine_t *)typeptr->engine;
        job->loadimage.id = typeptr->id;
        return GVRENDER_PLUGIN;
    }
    return NO_SUPPORT;
}

void gvloadimage(GVJ_t * job, usershape_t *us, boxf b, bool filled, const char *target)
{
    gvloadimage_engine_t *gvli;
    char type[SMALLBUF];

    assert(job);
    assert(us);
    assert(us->name);
    assert(us->name[0]);

    strcpy(type, us->stringtype);
    strcat(type, ":");
    strcat(type, target);

    if (gvloadimage_select(job, type) == NO_SUPPORT)
	    agerr (AGWARN, "No loadimage plugin for \"%s\"\n", type);

    if ((gvli = job->loadimage.engine) && gvli->loadimage)
	gvli->loadimage(job, us, b, filled);
}
