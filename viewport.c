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
viewport_draw(Viewport *v, Image *dst, char *rname)
{
	Point off, scale;

	off = Pt(v->p.x, v->p.y);
	/* no downsampling support yet */
	scale.x = max(v->bx.x, 1);
	scale.y = max(v->by.y, 1);

	v->fbctl->draw(v->fbctl, dst, rname, off, scale);
}

static void
viewport_memdraw(Viewport *v, Memimage *dst, char *rname)
{
	Point off, scale;

	off = Pt(v->p.x, v->p.y);
	/* no downsampling support yet */
	scale.x = max(v->bx.x, 1);
	scale.y = max(v->by.y, 1);

	v->fbctl->memdraw(v->fbctl, dst, rname, off, scale);
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

static void
viewport_createraster(Viewport *v, char *name, ulong chan)
{
	v->fbctl->createraster(v->fbctl, name, chan);
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

	if(badrect(r)){
		werrstr("bad viewport rectangle");
		return nil;
	}

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
	v->createraster = viewport_createraster;
	v->fetchraster = viewport_fetchraster;
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
