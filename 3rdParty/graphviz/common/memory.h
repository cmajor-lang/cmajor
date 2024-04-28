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

#define NEW(t)           (t*)zmalloc(sizeof(t))
#define N_NEW(n,t)       (t*)gcalloc((n),sizeof(t))
#define GNEW(t)          (t*)gmalloc(sizeof(t))

#define N_GNEW(n,t)      (t*)gcalloc((n),sizeof(t))
#define ALLOC(size,ptr,type) (ptr? (type*)grealloc(ptr,(size)*sizeof(type)):(type*)gmalloc((size)*sizeof(type)))
#define RALLOC(size,ptr,type) ((type*)grealloc(ptr,(size)*sizeof(type)))
#define ZALLOC(size,ptr,type,osize) (ptr? (type*)zrealloc(ptr,size,sizeof(type),osize):(type*)zmalloc((size)*sizeof(type)))
#ifdef GVDLL
#ifdef GVC_EXPORTS
#define MEMORY_API
#else
#define MEMORY_API
#endif
#endif

#ifndef MEMORY_API
#define MEMORY_API /* nothing */
#endif

    MEMORY_API void *zmalloc(size_t);
    MEMORY_API void *zrealloc(void *, size_t, size_t, size_t);
    MEMORY_API void *gcalloc(size_t nmemb, size_t size);
    MEMORY_API void *gmalloc(size_t);
    MEMORY_API void *grealloc(void *, size_t);
#undef MEMORY_API
