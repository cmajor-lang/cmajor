/**
 * @file
 * @brief @ref textspan_size
 */

/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "../cdt/cdt.h"
#include "render.h"
#include "textspan_lut.h"
#include "../cgraph/strcasecmp.h"

/* estimate_textspan_size:
 * Estimate size of textspan, for given face and size, in points.
 */
static void
estimate_textspan_size(textspan_t * span, char **fontpath)
{
    double fontsize;

    int flags = span->font->flags;
    bool bold = (flags & HTML_BF) != 0;
    bool italic = (flags & HTML_IF) != 0;

    fontsize = span->font->size;

    span->size.x = 0.0;
    span->size.y = fontsize * LINESPACING;
    span->yoffset_layout = 0.0;
    span->yoffset_centerline = 0.1 * fontsize;
    span->layout = NULL;
    span->free_layout = NULL;
    span->size.x = fontsize * estimate_text_width_1pt(span->font->name, span->str, bold, italic);

    if (fontpath)
        *fontpath = "[internal hard-coded]";
}

/*
 * This table maps standard Postscript font names to URW Type 1 fonts.
 */
static PostscriptAlias postscript_alias[] = {
#include "ps_font_equiv.h"
};

static int fontcmpf(const void *a, const void *b)
{
    return (strcasecmp(((const PostscriptAlias*)a)->name, ((const PostscriptAlias*)b)->name));
}

static PostscriptAlias* translate_postscript_fontname(char* fontname)
{
    static PostscriptAlias key;
    static PostscriptAlias *result;

    if (key.name == NULL || strcasecmp(key.name, fontname)) {
	free(key.name);
        key.name = strdup(fontname);
        result = (PostscriptAlias*)bsearch(&key, postscript_alias,
                         sizeof(postscript_alias) / sizeof(PostscriptAlias),
                         sizeof(PostscriptAlias), fontcmpf);
    }
    return result;
}

pointf textspan_size(GVC_t *gvc, textspan_t * span)
/// Estimates size of a textspan, in points.
{
    char **fpp = NULL, *fontpath = NULL;
    textfont_t *font;

    assert(span->font);
    font = span->font;

    assert(font->name);

    /* only need to find alias once per font, since they are unique in dict */
    if (! font->postscript_alias)
        font->postscript_alias = translate_postscript_fontname(font->name);

    if (Verbose && emit_once(font->name))
	fpp = &fontpath;

    if (! gvtextlayout(gvc, span, fpp))
	estimate_textspan_size(span, fpp);

    if (fpp) {
	if (fontpath)
	    fprintf(stderr, "fontname: \"%s\" resolved to: %s\n",
		    font->name, fontpath);
	else
	    fprintf(stderr, "fontname: unable to resolve \"%s\"\n", font->name);
    }

    return span->size;
}

static void* textfont_makef(Dt_t* dt, void* obj, Dtdisc_t* disc)
{
    (void)dt;
    (void)disc;

    textfont_t *f1 = (textfont_t*) obj;
    textfont_t *f2 = (textfont_t*)calloc(1,sizeof(textfont_t));

    /* key */
    if (f1->name) f2->name = strdup(f1->name);
    if (f1->color) f2->color = strdup(f1->color);
    f2->flags = f1->flags;
    f2->size = f1->size;

    /* non key */
    f2->postscript_alias = f1->postscript_alias;

    return f2;
}

static void textfont_freef(Dt_t* dt, void* obj, Dtdisc_t* disc)
{
    (void)dt;
    (void)disc;

    textfont_t *f =(textfont_t*) obj;

    free(f->name);
    free(f->color);
    free(f);
}

static int textfont_comparf (Dt_t* dt, void* key1, void* key2, Dtdisc_t* disc)
{
    (void)dt;
    (void)disc;

    int rc;
    textfont_t *f1 = (textfont_t*) key1, *f2 = (textfont_t*)key2;

    if (f1->name || f2->name) {
        if (! f1->name) return -1;
        if (! f2->name) return 1;
        rc = strcmp(f1->name, f2->name);
        if (rc) return rc;
    }
    if (f1->color || f2->color) {
        if (! f1->color) return -1;
        if (! f2->color) return 1;
        rc = strcmp(f1->color, f2->color);
        if (rc) return rc;
    }
    if (f1->flags < f2->flags) return -1;
    if (f1->flags > f2->flags) return 1;
    if (f1->size < f2->size) return -1;
    if (f1->size > f2->size) return 1;
    return 0;
}

Dt_t * textfont_dict_open(GVC_t *gvc)
{
    DTDISC(&(gvc->textfont_disc),0,sizeof(textfont_t),-1,textfont_makef,textfont_freef,textfont_comparf,NULL,NULL,NULL);
    gvc->textfont_dt = dtopen(&(gvc->textfont_disc), Dtoset);
    return gvc->textfont_dt;
}

void textfont_dict_close(GVC_t *gvc)
{
    dtclose(gvc->textfont_dt);
}
