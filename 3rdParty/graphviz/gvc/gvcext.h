/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

/* Common header used by both clients and plugins */

#pragma once

/*
 * Define an apis array of name strings using an enumerated api_t as index.
 * The enumerated type is defined here.  The apis array is
 * inititialized in gvplugin.c by redefining ELEM and reinvoking APIS.
 */
#define APIS ELEM(render) ELEM(layout) ELEM(textlayout) ELEM(device) ELEM(loadimage)

/*
 * Define api_t using names based on the plugin names with API_ prefixed.
 */
#define ELEM(x) API_##x,
    typedef enum { APIS } api_t; /* API_render, API_layout, ... */
#undef ELEM

    typedef struct GVJ_s GVJ_t;
    typedef struct GVG_s GVG_t;
    typedef struct GVC_s GVC_t;

    typedef struct {
    const char *name;
    void* address;
    } lt_symlist_t;

    typedef struct gvplugin_available_s gvplugin_available_t;

#if !defined(LTDL_H)
extern lt_symlist_t lt_preloaded_symbols[];
#endif
