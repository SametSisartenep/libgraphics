#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

static void
viewport_draw(Viewport *v, Image *dst)
{
	Point off, scale;

	off = Pt(v->p.x, v->p.y);
	/* no downsampling support yet */
	scale.x = max(min(v->bx.x, Dx(dst->r)/Dx(v->r)), 1);
	scale.y = max(min(v->by.y, Dy(dst->r)/Dy(v->r)), 1);

	if(scale.x > 1 || scale.y > 1)
		v->fbctl->upscaledraw(v->fbctl, dst, off, scale);
	else
		v->fbctl->draw(v->fbctl, dst, off);
}

static void
viewport_memdraw(Viewport *v, Memimage *dst)
{
	Point off, scale;

	off = Pt(v->p.x, v->p.y);
	/* no downsampling support yet */
	scale.x = max(min(v->bx.x, Dx(dst->r)/Dx(v->r)), 1);
	scale.y = max(min(v->by.y, Dy(dst->r)/Dy(v->r)), 1);

	if(scale.x > 1 || scale.y > 1)
		v->fbctl->upscalememdraw(v->fbctl, dst, off, scale);
	else
		v->fbctl->memdraw(v->fbctl, dst, off);
}

static void
viewport_setscale(Viewport *v, double sx, double sy)
{
	assert(sx > 0 && sy > 0);

	v->bx.x = sx;
	v->by.y = sy;
}

static void
viewport_setscalefilter(Viewport *v, int f)
{
	v->fbctl->upfilter = f;
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

Viewport *
mkviewport(Rectangle r)
{
	Viewport *v;

	v = emalloc(sizeof *v);
	v->p = Pt2(0,0,1);
	v->bx = Vec2(1,0);
	v->by = Vec2(0,1);
	v->fbctl = mkfbctl(r);
	v->r = r;
	v->draw = viewport_draw;
	v->memdraw = viewport_memdraw;
	v->setscale = viewport_setscale;
	v->setscalefilter = viewport_setscalefilter;
	v->getfb = viewport_getfb;
	v->getwidth = viewport_getwidth;
	v->getheight = viewport_getheight;
	return v;
}

void
rmviewport(Viewport *v)
{
	if(v == nil)
		return;
	rmfbctl(v->fbctl);
	free(v);
}
