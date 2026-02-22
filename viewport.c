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
	Viewdrawctx *ctx;
	Point off, scale;
	uvlong t0, t1;

	ctx = &v->dctx;
	if(ctx->img == nil){
		ctx->img = allocimage(display, v->r, RGBA32, 0, 0);
		if(ctx->img == nil)
			sysfatal("allocimage: %r");
	}

	off = Pt(v->p.x, v->p.y);
	/* no downsampling support yet */
	scale.x = v->bx.x;
	scale.y = v->by.y;

	t0 = nanosec();
	v->fbctl->draw(v->fbctl, dst, rname, off, scale, ctx);
	t1 = nanosec();
	updatestats(v, t1-t0);
}

static void
viewport_memdraw(Viewport *v, Memimage *dst, char *rname)
{
	Point off, scale;

	off = Pt(v->p.x, v->p.y);
	/* no downsampling support yet */
	scale.x = v->bx.x;
	scale.y = v->by.y;

	v->fbctl->memdraw(v->fbctl, dst, rname, off, scale, &v->dctx);
}

static void
viewport_setscale(Viewport *v, double sx, double sy)
{
	Viewdrawctx *ctx;

	assert(sx > 0 && sy > 0);

	v->bx.x = sx;
	v->by.y = sy;

	ctx = &v->dctx;
	if(sx > 1 || sy > 1){
		if(ctx->blk != nil)
			free(ctx->blk);
		ctx->blk = _emalloc(sx*Dx(v->r)*sy*4);
		ctx->blkr = Rect(0, 0, sx*Dx(v->r), sy);
	}else if(ctx->blk != nil){
		free(ctx->blk);
		ctx->blk = nil;
	}
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
	free(v->dctx.blk);
	freeimage(v->dctx.img);
	_rmfbctl(v->fbctl);
	free(v);
}
