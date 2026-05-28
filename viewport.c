#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

static void
viewport_draw(Viewport *v, Image *dst, char *rname)
{
	if(v->drawfb == nil){
		v->drawfb = allocimage(display, v->r, RGBA32, 0, 0);
		if(v->drawfb == nil)
			sysfatal("allocimage: %r");
	}

	v->fbctl->draw(v->fbctl, dst, rname, v);
}

static void
viewport_memdraw(Viewport *v, Memimage *dst, char *rname)
{
	v->fbctl->memdraw(v->fbctl, dst, rname, v);
}

static void
viewport_move(Viewport *v, Point2 p)
{
	Matrix m;

	v->p = p.w == 0? addpt2(v->p, p): p;
	rframematrix(m, *v);
	v->Warp = mkwarp(m);
}

static void
viewport_scale(Viewport *v, Point2 s)
{
	Matrix m;

	v->bx.x = s.x;
	v->by.y = s.y;
	rframematrix(m, *v);
	v->Warp = mkwarp(m);
}

static void
viewport_setfilter(Viewport *v, int f)
{
	v->filter = f;
}

static Framebuf *
viewport_getfb(Viewport *v)
{
	return v->fbctl->getfb(v->fbctl);
}

static int
viewport_getwidth(Viewport *v)
{
	return Dx(v->r)*v->bx.x;
}

static int
viewport_getheight(Viewport *v)
{
	return Dy(v->r)*v->by.y;
}

static int
viewport_createraster(Viewport *v, char *name, ulong chan)
{
	return v->fbctl->createraster(v->fbctl, name, chan);
}

static Raster *
viewport_fetchraster(Viewport *v, char *name)
{
	return v->fbctl->fetchraster(v->fbctl, name);
}

Viewport *
mkviewport(Rectangle r)
{
	Viewport *v;
	Matrix m;

	if(badrect(r)){
		werrstr("bad viewport rectangle");
		return nil;
	}

	v = _emalloc(sizeof *v);
	memset(v, 0, sizeof *v);
	v->p = Pt2(0,0,1);
	v->bx = Vec2(1,0);
	v->by = Vec2(0,1);
	rframematrix(m, *v);
	v->Warp = mkwarp(m);
	v->fbctl = _mkfbctl(r);
	v->r = r;
	v->draw = viewport_draw;
	v->memdraw = viewport_memdraw;
	v->move = viewport_move;
	v->scale = viewport_scale;
	v->setfilter = viewport_setfilter;
	v->getfb = viewport_getfb;
	v->getwidth = viewport_getwidth;
	v->getheight = viewport_getheight;
	v->createraster = viewport_createraster;
	v->fetchraster = viewport_fetchraster;
	return v;
}

void
rmviewport(Viewport *v)
{
	if(v == nil)
		return;
	freeimage(v->drawfb);
	_rmfbctl(v->fbctl);
	free(v);
}
