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
 *  textlayout engine wrapper
 */

#include "config.h"

#include "../common/const.h"
#include "../gvc/gvplugin_textlayout.h"
#include "../gvc/gvcint.h"
#include "../gvc/gvcproc.h"

int gvtextlayout_select(GVC_t * gvc)
{
    gvplugin_available_t *plugin;
    gvplugin_installed_t *typeptr;

    plugin = gvplugin_load(gvc, API_textlayout, "textlayout");
    if (plugin) {
	typeptr = plugin->typeptr;
	gvc->textlayout.engine = (gvtextlayout_engine_t *) typeptr->engine;
	return GVRENDER_PLUGIN;  /* FIXME - need more suitable success code */
    }
    return NO_SUPPORT;
}

bool gvtextlayout(GVC_t *gvc, textspan_t *span, char **fontpath)
{
    gvtextlayout_engine_t *gvte = gvc->textlayout.engine;

    if (gvte && gvte->textlayout)
	return gvte->textlayout(span, fontpath);
    return false;
}
