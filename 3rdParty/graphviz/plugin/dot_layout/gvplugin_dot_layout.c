/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "../../gvc/gvplugin.h"

extern gvplugin_installed_t gvlayout_dot_layout[];

static gvplugin_api_t apis[] = {
    {API_layout, gvlayout_dot_layout},
    {(api_t)0, 0},
};


#ifdef GVDLL
#define GVPLUGIN_DOT_LAYOUT_API
#else
#define GVPLUGIN_DOT_LAYOUT_API
#endif

GVPLUGIN_DOT_LAYOUT_API gvplugin_library_t gvplugin_dot_layout_LTX_library = { "dot_layout", apis };
