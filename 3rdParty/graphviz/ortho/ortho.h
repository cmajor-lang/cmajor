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

#include "../common/render.h"

#ifdef GVDLL
#ifdef GVC_EXPORTS
#define ORTHO_API
#else
#define ORTHO_API
#endif
#endif

#ifndef ORTHO_API
#define ORTHO_API /* nothing */
#endif

ORTHO_API void orthoEdges (Agraph_t* g, int useLbls);
