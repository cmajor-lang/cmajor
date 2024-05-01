/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

/* Header used by plugins */

#pragma once

#include "gvcext.h"

/*
 * Terminology:
 *
 *    package         - e.g. libgvplugin_cairo.so
 *       api          - e.g. render
 *          type      - e.g. "png", "ps"
 */

    typedef struct {
    int id;         /* an id that is only unique within a package
            of plugins of the same api.
            A renderer-type such as "png" in the cairo package
            has an id that is different from the "ps" type
            in the same package */
    const char *type;    /* a string name, such as "png" or "ps" that
            distinguishes different types within the same
             (renderer in this case) */
    int quality;    /* an arbitrary integer used for ordering plugins of
            the same type from different packages */
    void *engine;   /* pointer to the jump table for the plugin */
    void *features; /* pointer to the feature description
                void* because type varies by api */
    } gvplugin_installed_t;

    typedef struct {
    api_t api;
    gvplugin_installed_t *types;
    } gvplugin_api_t;

    typedef struct {
    const char *packagename;    /* used when this plugin is builtin and has
                    no pathname */
    gvplugin_api_t *apis;
    } gvplugin_library_t;
