#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

static void
updatestats(Viewport *c, uvlong v)
{
	c->stats.v = v;
	c->stats.n++;
	c->stats.acc += v;
	c->stats.avg = c->stats.acc/c->stats.n;
	c->stats.min = v < c->stats.min || c->stats.n == 1? v: c->stats.min;
	c->stats.max = v > c->stats.max || c->stats.n == 1? v: c->stats.max;
	c->stats.nframes++;
}

static void
viewport_draw(Viewport *v, Image *dst, char *rname)
{
	uvlong t0, t1;

	if(v->drawfb == nil){
		v->drawfb = allocimage(display, v->r, RGBA32, 0, 0);
		if(v->drawfb == nil)
			sysfatal("allocimage: %r");
	}

	t0 = nanosec();
	v->fbctl->draw(v->fbctl, dst, rname, v);
	t1 = nanosec();
	updatestats(v, t1-t0);
}

static void
viewport_memdraw(Viewport *v, Memimage *dst, char *rname)
{
	v->fbctl->memdraw(v->fbctl, dst, rname, v);
}

static void
viewport_setscale(Viewport *v, double sx, double sy)
{
	v->bx.x = sx;
	v->by.y = sy;
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

	if(badrect(r)){
		werrstr("bad viewport rectangle");
		return nil;
	}

	v = _emalloc(sizeof *v);
	memset(v, 0, sizeof *v);
	v->p = Pt2(0,0,1);
	v->bx = Vec2(1,0);
	v->by = Vec2(0,1);
	v->fbctl = _mkfbctl(r);
	v->r = r;
	v->draw = viewport_draw;
	v->memdraw = viewport_memdraw;
	v->setscale = viewport_setscale;
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
