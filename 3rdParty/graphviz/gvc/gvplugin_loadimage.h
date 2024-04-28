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
#include "../gvc/gvplugin.h"
#include "../gvc/gvcjob.h"

#ifdef GVDLL
#ifdef GVC_EXPORTS
#define GVPLUGIN_LOADIMAGE_API
#else
#define GVPLUGIN_LOADIMAGE_API
#endif
#endif

#ifndef GVPLUGIN_LOADIMAGE_API
#define GVPLUGIN_LOADIMAGE_API /* nothing */
#endif

GVPLUGIN_LOADIMAGE_API bool gvusershape_file_access(usershape_t *us);
GVPLUGIN_LOADIMAGE_API void gvusershape_file_release(usershape_t *us);

    struct gvloadimage_engine_s {
    void (*loadimage) (GVJ_t *job, usershape_t *us, boxf b, bool filled);
    };

#undef GVPLUGIN_LOADIMAGE_API
