/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "../../gvc/config.h"

#include "../../gvc/gvplugin_layout.h"

typedef enum { LAYOUT_DOT, } layout_type;

// extern void dot_layout(graph_t * g);
// extern void dot_cleanup(graph_t * g);

gvlayout_engine_t dotgen_engine = {
    dot_layout,
    dot_cleanup,
};


gvlayout_features_t dotgen_features = {
    LAYOUT_USES_RANKDIR,
};

gvplugin_installed_t gvlayout_dot_layout[] = {
    {LAYOUT_DOT, "dot", 0, &dotgen_engine, &dotgen_features},
    {0, NULL, 0, NULL, NULL}
};
