/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "../cgraph/agxbuf.h"
#include "../cgraph/alloc.h"
#include "../cgraph/prisize_t.h"
#include "../cgraph/unreachable.h"
#include "xdot.h"

/* the parse functions should return NULL on error */
static char *parseReal(char *s, double *fp)
{
    char *p;
    double d;

    d = strtod(s, &p);
    if (p == s) return 0;

    *fp = d;
    return (p);
}


static char *parseInt(char *s, int *ip)
{
    char* endp;

    *ip = (int)strtol (s, &endp, 10);
    if (s == endp)
	return 0;
    else
	return endp;
}

static char *parseUInt(char *s, unsigned int *ip)
{
    char* endp;

    *ip = (unsigned int)strtoul (s, &endp, 10);
    if (s == endp)
	return 0;
    else
	return endp;
}

static char *parseRect(char *s, xdot_rect * rp)
{
    char* endp;

    rp->x = strtod (s, &endp);
    if (s == endp)
	return 0;
    else
	s = endp;

    rp->y = strtod (s, &endp);
    if (s == endp)
	return 0;
    else
	s = endp;

    rp->w = strtod (s, &endp);
    if (s == endp)
	return 0;
    else
	s = endp;

    rp->h = strtod (s, &endp);
    if (s == endp)
	return 0;
    else
	s = endp;

    return s;
}

static char *parsePolyline(char *s, xdot_polyline * pp)
{
    unsigned i;
    xdot_point *pts;
    xdot_point *ps;
    char* endp;

    s = parseUInt(s, &i);
    if (!s) return NULL;
    pts = ps = (xdot_point*) gv_calloc(i, sizeof(ps[0]));
    pp->cnt = i;
    for (i = 0; i < pp->cnt; i++) {
	ps->x = strtod (s, &endp);
	if (s == endp) {
	    free (pts);
	    return NULL;
	}
	else
	    s = endp;
	ps->y = strtod (s, &endp);
	if (s == endp) {
	    free (pts);
	    return NULL;
	}
	else
	    s = endp;
	ps->z = 0;
	ps++;
    }
    pp->pts = pts;
    return s;
}

static char *parseString(char *s, char **sp)
{
    int i;
    s = parseInt(s, &i);
    if (!s || i <= 0) return 0;
    while (*s && *s != '-') s++;
    if (*s) s++;
    else {
	return 0;
    }

    char *c = gv_strndup(s, (size_t)i);
    if (strlen(c) != (size_t)i) {
	free (c);
	return 0;
    }

    *sp = c;
    return s + i;
}

static char *parseAlign(char *s, xdot_align * ap)
{
    int i;
    s = parseInt(s, &i);

    if (i < 0)
	*ap = xd_left;
    else if (i > 0)
	*ap = xd_right;
    else
	*ap = xd_center;
    return s;
}

#define CHK(s) if(!s){*error=1;return 0;}

static char *parseOp(xdot_op * op, char *s, drawfunc_t ops[], int* error)
{
    char* cs;
    xdot_color clr;

    *error = 0;
    while (isspace((int)*s))
	s++;
    switch (*s++) {
    case 'E':
	op->kind = xd_filled_ellipse;
	s = parseRect(s, &op->u.ellipse);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_ellipse];
	break;

    case 'e':
	op->kind = xd_unfilled_ellipse;
	s = parseRect(s, &op->u.ellipse);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_ellipse];
	break;

    case 'P':
	op->kind = xd_filled_polygon;
	s = parsePolyline(s, &op->u.polygon);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_polygon];
	break;

    case 'p':
	op->kind = xd_unfilled_polygon;
	s = parsePolyline(s, &op->u.polygon);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_polygon];
	break;

    case 'b':
	op->kind = xd_filled_bezier;
	s = parsePolyline(s, &op->u.bezier);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_bezier];
	break;

    case 'B':
	op->kind = xd_unfilled_bezier;
	s = parsePolyline(s, &op->u.bezier);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_bezier];
	break;

    case 'c':
	s = parseString(s, &cs);
	CHK(s);
	cs = parseXDotColor (cs, &clr);
	CHK(cs);
	if (clr.type == xd_none) {
	    op->kind = xd_pen_color;
	    op->u.color = clr.u.clr;
	    if (ops)
		op->drawfunc = ops[xop_pen_color];
	}
	else {
	    op->kind = xd_grad_pen_color;
	    op->u.grad_color = clr;
	    if (ops)
		op->drawfunc = ops[xop_grad_color];
	}
	break;

    case 'C':
	s = parseString(s, &cs);
	CHK(s);
	cs = parseXDotColor (cs, &clr);
	CHK(cs);
	if (clr.type == xd_none) {
	    op->kind = xd_fill_color;
	    op->u.color = clr.u.clr;
	    if (ops)
		op->drawfunc = ops[xop_fill_color];
	}
	else {
	    op->kind = xd_grad_fill_color;
	    op->u.grad_color = clr;
	    if (ops)
		op->drawfunc = ops[xop_grad_color];
	}
	break;

    case 'L':
	op->kind = xd_polyline;
	s = parsePolyline(s, &op->u.polyline);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_polyline];
	break;

    case 'T':
	op->kind = xd_text;
	s = parseReal(s, &op->u.text.x);
	CHK(s);
	s = parseReal(s, &op->u.text.y);
	CHK(s);
	s = parseAlign(s, &op->u.text.align);
	CHK(s);
	s = parseReal(s, &op->u.text.width);
	CHK(s);
	s = parseString(s, &op->u.text.text);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_text];
	break;

    case 'F':
	op->kind = xd_font;
	s = parseReal(s, &op->u.font.size);
	CHK(s);
	s = parseString(s, &op->u.font.name);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_font];
	break;

    case 'S':
	op->kind = xd_style;
	s = parseString(s, &op->u.style);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_style];
	break;

    case 'I':
	op->kind = xd_image;
	s = parseRect(s, &op->u.image.pos);
	CHK(s);
	s = parseString(s, &op->u.image.name);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_image];
	break;

    case 't':
	op->kind = xd_fontchar;
	s = parseUInt(s, &op->u.fontchar);
	CHK(s);
	if (ops)
	    op->drawfunc = ops[xop_fontchar];
	break;


    case '\0':
	s = 0;
	break;

    default:
	*error = 1;
	s = 0;
	break;
    }
    return s;
}

#define XDBSIZE 100

/* parseXDotFOn:
 * Parse and append additional xops onto a given xdot object.
 * Return x.
 */
xdot *parseXDotFOn(char *s, drawfunc_t fns[], size_t sz, xdot *x) {
    xdot_op op;
    char *ops;
    size_t oldsz, bufsz;
    int error;

    if (!s)
	return x;

    if (!x) {
	x = (xdot*) gv_alloc(sizeof(*x));
	if (sz <= sizeof(xdot_op))
	    sz = sizeof(xdot_op);

	/* cnt, freefunc, ops, flags zeroed by gv_alloc */
	x->sz = sz;
    }
    size_t initcnt = x->cnt;
    sz = x->sz;

    if (initcnt == 0) {
	bufsz = XDBSIZE;
	ops = (char*) gv_calloc(XDBSIZE, sz);
    }
    else {
	ops = (char*)(x->ops);
	bufsz = initcnt + XDBSIZE;
	ops = (char*) gv_recalloc(ops, initcnt, bufsz, sz);
    }

    while ((s = parseOp(&op, s, fns, &error))) {
	if (x->cnt == bufsz) {
	    oldsz = bufsz;
	    bufsz *= 2;
	    ops = (char*) gv_recalloc(ops, oldsz, bufsz, sz);
	}
	*(xdot_op *) (ops + x->cnt * sz) = op;
	x->cnt++;
    }
    if (error)
	x->flags |= XDOT_PARSE_ERROR;
    if (x->cnt) {
	x->ops = (xdot_op*)gv_recalloc(ops, bufsz, x->cnt, sz);
    }
    else {
	free (ops);
	free (x);
	x = NULL;
    }

    return x;

}

xdot *parseXDotF(char *s, drawfunc_t fns[], size_t sz) {
    return parseXDotFOn (s, fns, sz, NULL);
}

xdot *parseXDot(char *s)
{
    return parseXDotF(s, 0, 0);
}

typedef int (*pf)(void*, char*, ...);

/* trim:
 * Trailing zeros are removed and decimal point, if possible.
 */
static void trim (char* buf)
{
    char* dotp;
    char* p;

    if ((dotp = strchr (buf,'.'))) {
        p = dotp+1;
        while (*p) p++;  // find end of string
        p--;
        while (*p == '0') *p-- = '\0';
        if (*p == '.')        // If all decimals were zeros, remove ".".
            *p = '\0';
        else
            p++;
    }
}

static void printRect(xdot_rect * r, pf print, void *info)
{
    char buf[128];

    snprintf(buf, sizeof(buf), " %.02f", r->x);
    trim(buf);
    print(info, "%s", buf);
    snprintf(buf, sizeof(buf), " %.02f", r->y);
    trim(buf);
    print(info, "%s", buf);
    snprintf(buf, sizeof(buf), " %.02f", r->w);
    trim(buf);
    print(info, "%s", buf);
    snprintf(buf, sizeof(buf), " %.02f", r->h);
    trim(buf);
    print(info, "%s", buf);
}

static void printPolyline(xdot_polyline * p, pf print, void *info)
{
    char buf[512];

    print(info, " %" PRISIZE_T, p->cnt);
    for (size_t i = 0; i < p->cnt; i++) {
	snprintf(buf, sizeof(buf), " %.02f", p->pts[i].x);
	trim(buf);
	print(info, "%s", buf);
	snprintf(buf, sizeof(buf), " %.02f", p->pts[i].y);
	trim(buf);
	print(info, "%s", buf);
    }
}

static void printString(char *p, pf print, void *info)
{
    print(info, " %" PRISIZE_T " -%s", strlen(p), p);
}

static void printFloat(double f, pf print, void *info, int space) {
    char buf[128];

    if (space)
	snprintf(buf, sizeof(buf), " %.02f", f);
    else
	snprintf(buf, sizeof(buf), "%.02f", f);
    trim (buf);
    print(info, "%s", buf);
}

static void printAlign(xdot_align a, pf print, void *info)
{
    switch (a) {
    case xd_left:
	print(info, " -1");
	break;
    case xd_right:
	print(info, " 1");
	break;
    case xd_center:
	print(info, " 0");
	break;
    default:
	UNREACHABLE();
    }
}

static void
toGradString (agxbuf* xb, xdot_color* cp)
{
    int i, n_stops;
    xdot_color_stop* stops;

    if (cp->type == xd_linear) {
	agxbputc (xb, '[');
	printFloat (cp->u.ling.x0, (pf)agxbprint, xb, 0);
	printFloat (cp->u.ling.y0, (pf)agxbprint, xb, 1);
	printFloat (cp->u.ling.x1, (pf)agxbprint, xb, 1);
	printFloat (cp->u.ling.y1, (pf)agxbprint, xb, 1);
	n_stops = cp->u.ling.n_stops;
	stops = cp->u.ling.stops;
    }
    else {
	agxbputc (xb, '(');
	printFloat (cp->u.ring.x0, (pf)agxbprint, xb, 0);
	printFloat (cp->u.ring.y0, (pf)agxbprint, xb, 1);
	printFloat (cp->u.ring.r0, (pf)agxbprint, xb, 1);
	printFloat (cp->u.ring.x1, (pf)agxbprint, xb, 1);
	printFloat (cp->u.ring.y1, (pf)agxbprint, xb, 1);
	printFloat (cp->u.ring.r1, (pf)agxbprint, xb, 1);
	n_stops = cp->u.ring.n_stops;
	stops = cp->u.ring.stops;
    }
    agxbprint(xb, " %d", n_stops);
    for (i = 0; i < n_stops; i++) {
	printFloat (stops[i].frac, (pf)agxbprint, xb, 1);
	printString (stops[i].color, (pf)agxbprint, xb);
    }

    if (cp->type == xd_linear)
	agxbputc (xb, ']');
    else
	agxbputc (xb, ')');
}

typedef void (*print_op)(xdot_op * op, pf print, void *info, int more);

static void printXDot_Op(xdot_op * op, pf print, void *info, int more)
{
    agxbuf xb;
    char buf[BUFSIZ];

    agxbinit (&xb, BUFSIZ, buf);
    switch (op->kind) {
    case xd_filled_ellipse:
	print(info, "E");
	printRect(&op->u.ellipse, print, info);
	break;
    case xd_unfilled_ellipse:
	print(info, "e");
	printRect(&op->u.ellipse, print, info);
	break;
    case xd_filled_polygon:
	print(info, "P");
	printPolyline(&op->u.polygon, print, info);
	break;
    case xd_unfilled_polygon:
	print(info, "p");
	printPolyline(&op->u.polygon, print, info);
	break;
    case xd_filled_bezier:
	print(info, "b");
	printPolyline(&op->u.bezier, print, info);
	break;
    case xd_unfilled_bezier:
	print(info, "B");
	printPolyline(&op->u.bezier, print, info);
	break;
    case xd_pen_color:
	print(info, "c");
	printString(op->u.color, print, info);
	break;
    case xd_grad_pen_color:
	print(info, "c");
	toGradString (&xb, &op->u.grad_color);
	printString(agxbuse(&xb), print, info);
	break;
    case xd_fill_color:
	print(info, "C");
	printString(op->u.color, print, info);
	break;
    case xd_grad_fill_color:
	print(info, "C");
	toGradString (&xb, &op->u.grad_color);
	printString(agxbuse(&xb), print, info);
	break;
    case xd_polyline:
	print(info, "L");
	printPolyline(&op->u.polyline, print, info);
	break;
    case xd_text:
	print(info, "T %.f %.f", op->u.text.x, op->u.text.y);
	printAlign(op->u.text.align, print, info);
	print(info, " %.f", op->u.text.width);
	printString(op->u.text.text, print, info);
	break;
    case xd_font:
	print(info, "F");
	printFloat(op->u.font.size, print, info, 1);
	printString(op->u.font.name, print, info);
	break;
    case xd_fontchar:
	print(info, "t %u", op->u.fontchar);
	break;
    case xd_style:
	print(info, "S");
	printString(op->u.style, print, info);
	break;
    case xd_image:
	print(info, "I");
	printRect(&op->u.image.pos, print, info);
	printString(op->u.image.name, print, info);
	break;
    }
    if (more)
	print(info, " ");
    agxbfree (&xb);
}

static void jsonRect(xdot_rect * r, pf print, void *info)
{
  print(info, "[%.06f,%.06f,%.06f,%.06f]", r->x, r->y, r->w, r->h);
}

static void jsonPolyline(xdot_polyline * p, pf print, void *info)
{
    print(info, "[");
    for (size_t i = 0; i < p->cnt; i++) {
	print(info, "%.06f,%.06f", p->pts[i].x, p->pts[i].y);
	if (i < p->cnt-1) print(info, ",");
    }
    print(info, "]");
}

static void jsonString(char *p, pf print, void *info)
{
    char c;

    print(info, "\"");
    while ((c = *p++)) {
	if (c == '"') print(info, "\\\"");
	else if (c == '\\') print(info, "\\\\");
	else print(info, "%c", c);
    }
    print(info, "\"");
}

static void jsonXDot_Op(xdot_op * op, pf print, void *info, int more)
{
    agxbuf xb;
    char buf[BUFSIZ];

    agxbinit (&xb, BUFSIZ, buf);
    switch (op->kind) {
    case xd_filled_ellipse:
	print(info, "{\"E\" : ");
	jsonRect(&op->u.ellipse, print, info);
	break;
    case xd_unfilled_ellipse:
	print(info, "{\"e\" : ");
	jsonRect(&op->u.ellipse, print, info);
	break;
    case xd_filled_polygon:
	print(info, "{\"P\" : ");
	jsonPolyline(&op->u.polygon, print, info);
	break;
    case xd_unfilled_polygon:
	print(info, "{\"p\" : ");
	jsonPolyline(&op->u.polygon, print, info);
	break;
    case xd_filled_bezier:
	print(info, "{\"b\" : ");
	jsonPolyline(&op->u.bezier, print, info);
	break;
    case xd_unfilled_bezier:
	print(info, "{\"B\" : ");
	jsonPolyline(&op->u.bezier, print, info);
	break;
    case xd_pen_color:
	print(info, "{\"c\" : ");
	jsonString(op->u.color, print, info);
	break;
    case xd_grad_pen_color:
	print(info, "{\"c\" : ");
	toGradString (&xb, &op->u.grad_color);
	jsonString(agxbuse(&xb), print, info);
	break;
    case xd_fill_color:
	print(info, "{\"C\" : ");
	jsonString(op->u.color, print, info);
	break;
    case xd_grad_fill_color:
	print(info, "{\"C\" : ");
	toGradString (&xb, &op->u.grad_color);
	jsonString(agxbuse(&xb), print, info);
	break;
    case xd_polyline:
	print(info, "{\"L\" :");
	jsonPolyline(&op->u.polyline, print, info);
	break;
    case xd_text:
	print(info, "{\"T\" : [ %.f, %.f,", op->u.text.x, op->u.text.y);
	printAlign(op->u.text.align, print, info);
	print(info, ", %.f,", op->u.text.width);
	jsonString(op->u.text.text, print, info);
	print(info, "]");
	break;
    case xd_font:
	print(info, "{\"F\" : [");
	op->kind = xd_font;
	printFloat(op->u.font.size, print, info, 1);
	print(info, ",");
	jsonString(op->u.font.name, print, info);
	print(info, "]");
	break;
    case xd_fontchar:
	print(info, "{\"t\" :  %u", op->u.fontchar);
	break;
    case xd_style:
	print(info, "{\"S\" : ");
	jsonString(op->u.style, print, info);
	break;
    case xd_image:
	print(info, "{\"I\" : [");
	jsonRect(&op->u.image.pos, print, info);
	print(info, ",");
	jsonString(op->u.image.name, print, info);
	print(info, "]");
	break;
    }
    if (more)
	print(info, "},\n");
    else
	print(info, "}\n");
    agxbfree (&xb);
}

static void _printXDot(xdot * x, pf print, void *info, print_op ofn)
{
    xdot_op *op;
    char *base = (char *) (x->ops);
    for (size_t i = 0; i < x->cnt; i++) {
	op = (xdot_op *) (base + i * x->sz);
	ofn(op, print, info, i < x->cnt - 1);
    }
}

char *sprintXDot(xdot * x)
{
    char buf[BUFSIZ];
    agxbuf xb;
    agxbinit(&xb, BUFSIZ, buf);
    _printXDot(x, (pf)agxbprint, &xb, printXDot_Op);
    return agxbdisown(&xb);
}

void fprintXDot(FILE * fp, xdot * x)
{
    _printXDot(x, (pf)fprintf, fp, printXDot_Op);
}

void jsonXDot(FILE * fp, xdot * x)
{
    fputs ("[\n", fp);
    _printXDot(x, (pf)fprintf, fp, jsonXDot_Op);
    fputs ("]\n", fp);
}

static void freeXOpData(xdot_op * x)
{
    switch (x->kind) {
    case xd_filled_polygon:
    case xd_unfilled_polygon:
	free(x->u.polyline.pts);
	break;
    case xd_filled_bezier:
    case xd_unfilled_bezier:
	free(x->u.polyline.pts);
	break;
    case xd_polyline:
	free(x->u.polyline.pts);
	break;
    case xd_text:
	free(x->u.text.text);
	break;
    case xd_fill_color:
    case xd_pen_color:
	free(x->u.color);
	break;
    case xd_grad_fill_color:
    case xd_grad_pen_color:
	freeXDotColor (&x->u.grad_color);
	break;
    case xd_font:
	free(x->u.font.name);
	break;
    case xd_style:
	free(x->u.style);
	break;
    case xd_image:
	free(x->u.image.name);
	break;
    default:
	break;
    }
}

void freeXDot (xdot * x)
{
    xdot_op *op;
    char *base;
    freefunc_t ff = x->freefunc;

    if (!x) return;
    base = (char *) (x->ops);
    for (size_t i = 0; i < x->cnt; i++) {
	op = (xdot_op *) (base + i * x->sz);
	if (ff) ff (op);
	freeXOpData(op);
    }
    free(base);
    free(x);
}

int statXDot (xdot* x, xdot_stats* sp)
{
    xdot_op *op;
    char *base;

    if (!x || !sp) return 1;
    memset(sp, 0, sizeof(xdot_stats));
    sp->cnt = x->cnt;
    base = (char *) (x->ops);
    for (size_t i = 0; i < x->cnt; i++) {
	op = (xdot_op *) (base + i * x->sz);
 	switch (op->kind) {
	case xd_filled_ellipse:
	case xd_unfilled_ellipse:
	    sp->n_ellipse++;
	    break;
	case xd_filled_polygon:
	case xd_unfilled_polygon:
	    sp->n_polygon++;
	    sp->n_polygon_pts += op->u.polygon.cnt;
	    break;
	case xd_filled_bezier:
	case xd_unfilled_bezier:
	    sp->n_bezier++;
	    sp->n_bezier_pts += op->u.bezier.cnt;
	    break;
	case xd_polyline:
	    sp->n_polyline++;
	    sp->n_polyline_pts += op->u.polyline.cnt;
	    break;
	case xd_text:
	    sp->n_text++;
	    break;
	case xd_image:
	    sp->n_image++;
	    break;
	case xd_fill_color:
	case xd_pen_color:
	    sp->n_color++;
	    break;
	case xd_grad_fill_color:
	case xd_grad_pen_color:
	    sp->n_gradcolor++;
	    break;
        case xd_font:
	    sp->n_font++;
	    break;
        case xd_fontchar:
	    sp->n_fontchar++;
	    break;
	case xd_style:
	    sp->n_style++;
	    break;
	default :
	    break;
	}
    }

    return 0;
}

#define CHK1(s) if(!s){free(stops);return NULL;}

/* radGradient:
 * Parse radial gradient spec
 * Return NULL on failure.
 */
static char*
radGradient (char* cp, xdot_color* clr)
{
    char* s = cp;
    int i;
    double d;
    xdot_color_stop* stops = NULL;

    clr->type = xd_radial;
    s = parseReal(s, &clr->u.ring.x0);
    CHK1(s);
    s = parseReal(s, &clr->u.ring.y0);
    CHK1(s);
    s = parseReal(s, &clr->u.ring.r0);
    CHK1(s);
    s = parseReal(s, &clr->u.ring.x1);
    CHK1(s);
    s = parseReal(s, &clr->u.ring.y1);
    CHK1(s);
    s = parseReal(s, &clr->u.ring.r1);
    CHK1(s);
    s = parseInt(s, &clr->u.ring.n_stops);
    CHK1(s);

    stops = (xdot_color_stop*) gv_calloc(clr->u.ring.n_stops, sizeof(stops[0]));
    for (i = 0; i < clr->u.ring.n_stops; i++) {
	s = parseReal(s, &d);
	CHK1(s);
	stops[i].frac = d;
	s = parseString(s, &stops[i].color);
	CHK1(s);
    }
    clr->u.ring.stops = stops;

    return cp;
}

/* linGradient:
 * Parse linear gradient spec
 * Return NULL on failure.
 */
static char*
linGradient (char* cp, xdot_color* clr)
{
    char* s = cp;
    int i;
    double d;
    xdot_color_stop* stops = NULL;

    clr->type = xd_linear;
    s = parseReal(s, &clr->u.ling.x0);
    CHK1(s);
    s = parseReal(s, &clr->u.ling.y0);
    CHK1(s);
    s = parseReal(s, &clr->u.ling.x1);
    CHK1(s);
    s = parseReal(s, &clr->u.ling.y1);
    CHK1(s);
    s = parseInt(s, &clr->u.ling.n_stops);
    CHK1(s);

    stops = (xdot_color_stop*) gv_calloc(clr->u.ling.n_stops, sizeof(stops[0]));
    for (i = 0; i < clr->u.ling.n_stops; i++) {
	s = parseReal(s, &d);
	CHK1(s);
	stops[i].frac = d;
	s = parseString(s, &stops[i].color);
	CHK1(s);
    }
    clr->u.ling.stops = stops;

    return cp;
}

/* parseXDotColor:
 * Parse xdot color spec: ordinary or gradient
 * The result is stored in clr.
 * Return NULL on failure.
 */
char*
parseXDotColor (char* cp, xdot_color* clr)
{
    char c = *cp;

    switch (c) {
    case '[' :
	return linGradient (cp+1, clr);
	break;
    case '(' :
	return radGradient (cp+1, clr);
	break;
    case '#' :
    case '/' :
	clr->type = xd_none;
	clr->u.clr = cp;
	return cp;
	break;
    default :
	if (isalnum(c)) {
	    clr->type = xd_none;
	    clr->u.clr = cp;
	    return cp;
	}
	else
	    return NULL;
    }
}

void freeXDotColor (xdot_color* cp)
{
    int i;

    if (cp->type == xd_linear) {
	for (i = 0; i < cp->u.ling.n_stops; i++) {
	    free (cp->u.ling.stops[i].color);
	}
	free (cp->u.ling.stops);
    }
    else if (cp->type == xd_radial) {
	for (i = 0; i < cp->u.ring.n_stops; i++) {
	    free (cp->u.ring.stops[i].color);
	}
	free (cp->u.ring.stops);
    }
}

/**
 * @dir lib/xdot
 * @brief API xdot/xdot.h
 */
