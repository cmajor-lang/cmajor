/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#pragma once

#include "../common/types.h"
#include "gvplugin.h"
#include "gvcjob.h"

    struct gvlayout_engine_s {
    void (*layout) (graph_t * g);
    void (*cleanup) (graph_t * g);
    };
