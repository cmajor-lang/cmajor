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

#include "../gvc/config.h"

#include "color.h"

#ifdef GVDLL
#ifdef GVC_EXPORTS
#define COLORPROCS_API
#else
#define COLORPROCS_API extern
#endif
#endif

#ifndef COLORPROCS_API
#define COLORPROCS_API /* nothing */
#endif

COLORPROCS_API void setColorScheme (char* s);
COLORPROCS_API int colorxlate(char *str, gvcolor_t * color, color_type_t target_type);
COLORPROCS_API char *canontoken(char *str);

#undef COLORPROCS_API
