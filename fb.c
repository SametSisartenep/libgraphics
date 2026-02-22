#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

/*
 * scale[234]x filters
 *
 * see https://www.scale2x.it/algorithm
 */
static void
scale2x_filter(ulong *dst, Raster *fb, Point sp, Point, ulong dx)
{
	ulong B, D, E, F, H;

	E = getpixel(fb, sp);
	B = sp.y == fb->r.min.y? E: getpixel(fb, addpt(sp, Pt( 0,-1)));
	D = sp.x == fb->r.min.x? E: getpixel(fb, addpt(sp, Pt(-1, 0)));
	F = sp.x == fb->r.max.x? E: getpixel(fb, addpt(sp, Pt( 1, 0)));
	H = sp.y == fb->r.max.y? E: getpixel(fb, addpt(sp, Pt( 0, 1)));

	if(B != H && D != F){
		dst[0*dx+0] = D == B? D: E;
		dst[0*dx+1] = B == F? F: E;
		dst[1*dx+0] = D == H? D: E;
		dst[1*dx+1] = H == F? F: E;
	}else{
		_memsetl(dst + 0*dx, E, 2);
		_memsetl(dst + 1*dx, E, 2);
	}
}

static void
scale3x_filter(ulong *dst, Raster *fb, Point sp, Point, ulong dx)
{
	ulong A, B, C, D, E, F, G, H, I;

	E = getpixel(fb, sp);
	B = sp.y == fb->r.min.y? E: getpixel(fb, addpt(sp, Pt( 0,-1)));
	D = sp.x == fb->r.min.x? E: getpixel(fb, addpt(sp, Pt(-1, 0)));
	F = sp.x == fb->r.max.x? E: getpixel(fb, addpt(sp, Pt( 1, 0)));
	H = sp.y == fb->r.max.y? E: getpixel(fb, addpt(sp, Pt( 0, 1)));
	A = sp.y == fb->r.min.y && sp.x == fb->r.min.x? E:
		sp.y == fb->r.min.y? D: sp.x == fb->r.min.x? B:
		getpixel(fb, addpt(sp, Pt(-1,-1)));
	C = sp.y == fb->r.min.y && sp.x == fb->r.max.x? E:
		sp.y == fb->r.min.y? F: sp.x == fb->r.max.x? B:
		getpixel(fb, addpt(sp, Pt( 1,-1)));
	G = sp.y == fb->r.max.y && sp.x == fb->r.min.x? E:
		sp.y == fb->r.max.y? D: sp.x == fb->r.min.x? H:
		getpixel(fb, addpt(sp, Pt(-1, 1)));
	I = sp.y == fb->r.max.y && sp.x == fb->r.max.x? E:
		sp.y == fb->r.max.y? F: sp.x == fb->r.max.x? H:
		getpixel(fb, addpt(sp, Pt( 1, 1)));

	if(B != H && D != F){
		dst[0*dx+0] = D == B? D: E;
		dst[0*dx+1] = (D == B && E != C) || (B == F && E != A)? B: E;
		dst[0*dx+2] = B == F? F: E;
		dst[1*dx+0] = (D == B && E != G) || (D == H && E != A)? D: E;
		dst[1*dx+1] = E;
		dst[1*dx+2] = (B == F && E != I) || (H == F && E != C)? F: E;
		dst[2*dx+0] = D == H? D: E;
		dst[2*dx+1] = (D == H && E != I) || (H == F && E != G)? H: E;
		dst[2*dx+2] = H == F? F: E;
	}else{
		_memsetl(dst + 0*dx, E, 3);
		_memsetl(dst + 1*dx, E, 3);
		_memsetl(dst + 2*dx, E, 3);
	}
}

//static void
//scale4x_filter(ulong *dst, Raster *fb, Point sp, ulong)
//{
//
//}

static void
ident_filter(ulong *dst, Raster *fb, Point sp, Point s, ulong dx)
{
	ulong c;
	int y;

	c = getpixel(fb, sp);
	for(y = 0; y < s.y; y++)
		_memsetl(dst + y*dx, c, s.x);
}

/* convert a float raster to a greyscale color one */
static void
rasterconvF2C(Raster *dst, Raster *src)
{
	ulong *c, len;
	float *f, min, max;
	uchar b;

	/* first run: get the domain */
	f = (float*)src->data;
	len = Dx(dst->r)*Dy(dst->r);
	for(min = max = 0; len--; f++){
		if(isInf(*f, -1))	/* -∞ is the DNotacolor of the z-buffer */
			continue;
		min = min(*f, min);
		max = max(*f, max);
	}
	/* center it at zero: [min, max] → [0, max-min]*/
	max -= min;
	if(max == 0)
		max = 1;

	/* second run: average the values */
	c = dst->data;
	f = (float*)src->data;
	len = Dx(dst->r)*Dy(dst->r);
	while(len--){
		if(isInf(*f, -1)){	/* -∞ is the DNotacolor of the z-buffer */
			*c++ = 0x00;
			f++;
			continue;
		}
		b = (*f++ - min)/max * 0xFF;
		*c++ = (b * 0x01010100) | 0xFF;
	}
}

static void
upscaledraw(Raster *fb, Image *dst, Point off, Point scale, uint filter, Viewdrawctx *ctx)
{
	void (*filterfn)(ulong*, Raster*, Point, Point, ulong);
	Rectangle blkr, dr;
	Point sp, dp;
	Image *tmp;
	ulong *blk, *blkp;
	int dx, nl;

	filterfn = nil;
	blk = ctx->blk;
	blkr = ctx->blkr;
	dx = Dx(blkr);
	tmp = allocimage(display, dst->r, RGBA32, 0, 0);
	if(tmp == nil)
		sysfatal("allocimage: %r");

	switch(filter){
	case UFScale2x:
		if(scale.x == scale.y && scale.y == 2)
			filterfn = scale2x_filter;
		break;
	case UFScale3x:
		if(scale.x == scale.y && scale.y == 3)
			filterfn = scale3x_filter;
		break;
	default:
		filterfn = ident_filter;
	}

	/*
	 * Fill an entire upscaled line (blk) and then clip against
	 * the dst->r to select the data that needs to be sent.
	 *
	 * A better version could also do an initial clip to determine
	 * the scanline segments that need to be sampled in the first
	 * place.
	 */
	blkr = rectaddpt(blkr, addpt(dst->r.min, off));
	for(sp.y = fb->r.min.y; sp.y < fb->r.max.y; sp.y++){
	for(sp.x = fb->r.min.x, blkp = blk; sp.x < fb->r.max.x; sp.x++, blkp += scale.x){
		filterfn(blkp, fb, sp, scale, dx);
	}
		dr = blkr;
		if(rectclip(&dr, dst->r)){
			/* get blk local (data) point */
			dp.x = dr.min.x > blkr.min.x? dr.min.x - blkr.min.x: 0;
			dp.y = dr.min.y > blkr.min.y? dr.min.y - blkr.min.y: 0;
			nl = Dy(dr);
			dr.max.y = dr.min.y+1;
			for(; nl-- > 0; dp.y++, dr.min.y++, dr.max.y++)
				loadimage(tmp, dr, (uchar*)(blk + dp.y*dx + dp.x), Dx(dr)*4);
		}
		blkr.min.y += scale.y;
		blkr.max.y += scale.y;
	}
	draw(dst, dst->r, tmp, nil, tmp->r.min);
	freeimage(tmp);
}

static void
framebufctl_draw(Framebufctl *ctl, Image *dst, char *name, Point off, Point scale, Viewdrawctx *ctx)
{
	Framebuf *fb;
	Raster *r, *r2;
	Image *tmp;
	Rectangle sr, dr, lr;

	qlock(ctl);
	fb = ctl->getfb(ctl);

	r = fb->fetchraster(fb, name);
	if(r == nil){
		qunlock(ctl);
		return;
	}

	r2 = nil;
	if(r->chan == FLOAT32){
		r2 = _allocraster(nil, r->r, COLOR32);
		rasterconvF2C(r2, r);
		r = r2;
	}

	if(scale.x > 1 || scale.y > 1){
		upscaledraw(r, dst, off, scale, ctl->upfilter, ctx);
		qunlock(ctl);
		_freeraster(r2);
		return;
	}

	/* TODO use the clipr in upscaledraw too */
	tmp = ctx->img;
	sr = rectaddpt(fb->clipr, off);
	dr = rectsubpt(dst->r, dst->r.min);
	if(rectclip(&sr, dr)){
		dr = rectaddpt(sr, dst->r.min);
		/* remove offset to get the actual rect within the framebuffer */
		lr = sr = rectsubpt(sr, off);
		lr.max.y = lr.min.y + 1;
		for(; lr.min.y < sr.max.y; lr.min.y++, lr.max.y++)
			loadimage(tmp, lr, _rasterbyteaddr(r, lr.min), Dx(sr)*4);
		draw(dst, dr, tmp, nil, sr.min);
//		border(dst, dr, 1, display->black, ZP);
//		border(dst, rectaddpt(sr, dst->r.min), 1, display->white, ZP);
	}
	qunlock(ctl);
	_freeraster(r2);
}

static void
upscalememdraw(Raster *fb, Memimage *dst, Point off, Point scale, uint filter, Viewdrawctx *ctx)
{
	void (*filterfn)(ulong*, Raster*, Point, Point, ulong);
	Rectangle blkr, dr;
	Point sp, dp;
	Memimage *tmp;
	ulong *blk, *blkp;
	int dx, nl;

	filterfn = nil;
	blk = ctx->blk;
	blkr = ctx->blkr;
	dx = Dx(blkr);
	tmp = _eallocmemimage(dst->r, RGBA32);

	switch(filter){
	case UFScale2x:
		if(scale.x == scale.y && scale.y == 2)
			filterfn = scale2x_filter;
		break;
	case UFScale3x:
		if(scale.x == scale.y && scale.y == 3)
			filterfn = scale3x_filter;
		break;
	default:
		filterfn = ident_filter;
	}

	/*
	 * Fill an entire upscaled line (blk) and then clip against
	 * the dst->r to select the data that needs to be loaded.
	 *
	 * A better version could also do an initial clip to determine
	 * the scanline segments that need to be sampled in the first
	 * place.
	 */
	blkr = rectaddpt(blkr, addpt(dst->r.min, off));
	for(sp.y = fb->r.min.y; sp.y < fb->r.max.y; sp.y++){
	for(sp.x = fb->r.min.x, blkp = blk; sp.x < fb->r.max.x; sp.x++, blkp += scale.x){
		filterfn(blkp, fb, sp, scale, dx);
	}
		dr = blkr;
		if(rectclip(&dr, dst->r)){
			/* get blk local (data) point */
			dp.x = dr.min.x > blkr.min.x? dr.min.x - blkr.min.x: 0;
			dp.y = dr.min.y > blkr.min.y? dr.min.y - blkr.min.y: 0;
			nl = Dy(dr);
			dr.max.y = dr.min.y+1;
			for(; nl-- > 0; dp.y++, dr.min.y++, dr.max.y++)
				loadmemimage(tmp, dr, (uchar*)(blk + dp.y*dx + dp.x), Dx(dr)*4);
		}
		blkr.min.y += scale.y;
		blkr.max.y += scale.y;
	}
	memimagedraw(dst, dst->r, tmp, tmp->r.min, nil, ZP, SoverD);
	freememimage(tmp);
}

static void
framebufctl_memdraw(Framebufctl *ctl, Memimage *dst, char *name, Point off, Point scale, Viewdrawctx *ctx)
{
	Framebuf *fb;
	Raster *r, *r2;
	Rectangle sr, dr;
	Memimage *tmp;
	uchar *bdata0;

	qlock(ctl);
	fb = ctl->getfb(ctl);

	r = fb->fetchraster(fb, name);
	if(r == nil){
		qunlock(ctl);
		return;
	}

	r2 = nil;
	if(r->chan == FLOAT32){
		r2 = _allocraster(nil, r->r, COLOR32);
		rasterconvF2C(r2, r);
		r = r2;
	}

	if(scale.x > 1 || scale.y > 1){
		upscalememdraw(r, dst, off, scale, ctl->upfilter, ctx);
		qunlock(ctl);
		_freeraster(r2);
		return;
	}

	sr = rectaddpt(fb->r, off);
	dr = rectsubpt(dst->r, dst->r.min);
	if(rectinrect(sr, dr)){
		tmp = _eallocmemimage(sr, RGBA32);
		bdata0 = tmp->data->bdata;
		tmp->data->bdata = (void*)r->data;
		memimagedraw(dst, rectaddpt(sr, dst->r.min), tmp, ZP, nil, ZP, SoverD);
		tmp->data->bdata = bdata0;
		freememimage(tmp);
	}else if(rectclip(&sr, dr)){
		tmp = _eallocmemimage(sr, RGBA32);
		dr = rectaddpt(sr, dst->r.min);
		dr.max.y = dr.min.y + 1;
		/* remove offset to get the actual rect within the framebuffer */
		sr = rectsubpt(sr, off);
		for(; sr.min.y < sr.max.y; sr.min.y++, dr.min.y++, dr.max.y++)
			loadmemimage(tmp, dr, _rasterbyteaddr(r, sr.min), Dx(dr)*4);
		memimagedraw(dst, rectaddpt(tmp->r, dst->r.min), tmp, tmp->r.min, nil, ZP, SoverD);
		freememimage(tmp);
	}
	qunlock(ctl);
	_freeraster(r2);
}

static void
framebufctl_swap(Framebufctl *ctl)
{
	qlock(ctl);
	ctl->idx ^= 1;
	qunlock(ctl);
}

static void
resetAbuf(Abuf *buf)
{
	while(buf->nact--)
		free(buf->act[buf->nact]->items);
	free(buf->act);
	free(buf->stk);
	memset(buf, 0, sizeof *buf);
}

static void
framebufctl_reset(Framebufctl *ctl)
{
	Framebuf *fb;
	Raster *r;

	/* address the back buffer—resetting the front buffer is VERBOTEN */
	fb = ctl->getbb(ctl);
	resetAbuf(&fb->abuf);

	r = fb->rasters;		/* color buffer */
	_clearraster(r, 0);
	r = r->next;			/* z-buffer */
	_fclearraster(r, Inf(-1));
	while((r = r->next) != nil)
		_clearraster(r, 0);	/* every other raster */
}

static Framebuf *
framebufctl_getfb(Framebufctl *ctl)
{
	return ctl->fb[ctl->idx];	/* front buffer */
}

static Framebuf *
framebufctl_getbb(Framebufctl *ctl)
{
	return ctl->fb[ctl->idx^1];	/* back buffer */
}

static void
framebufctl_createraster(Framebufctl *ctl, char *name, ulong chan)
{
	Framebuf **fb;

	for(fb = ctl->fb; fb < ctl->fb+2; fb++)
		(*fb)->createraster(*fb, name, chan);
}

static Raster *
framebufctl_fetchraster(Framebufctl *ctl, char *name)
{
	Framebuf *fb;

	fb = ctl->getfb(ctl);
	return fb->fetchraster(fb, name);
}

static void
fb_createraster(Framebuf *fb, char *name, ulong chan)
{
	Raster *r;

	assert(name != nil);

	/*
	 * TODO might be better to keep a tail so it's O(1)
	 *
	 * in practice though, most users won't ever create
	 * more than ten extra rasters.
	 */
	r = fb->rasters;
	while(r->next != nil)
		r = r->next;
	r->next = _allocraster(name, fb->r, chan);
}

static Raster *
fb_fetchraster(Framebuf *fb, char *name)
{
	Raster *r;

	r = fb->rasters;
	if(name == nil)
		return r;

	while((r = r->next) != nil)
		if(strcmp(name, r->name) == 0)
			return r;
	return nil;
}

Framebuf *
_mkfb(Rectangle r)
{
	Framebuf *fb;

	fb = _emalloc(sizeof *fb);
	memset(fb, 0, sizeof *fb);
	fb->rasters = _allocraster(nil, r, COLOR32);
	fb->rasters->next = _allocraster("z-buffer", r, FLOAT32);
	fb->r = r;
	fb->createraster = fb_createraster;
	fb->fetchraster = fb_fetchraster;
	return fb;
}

void
_rmfb(Framebuf *fb)
{
	Raster *r, *nr;

	for(r = fb->rasters; r != nil; r = nr){
		nr = r->next;
		_freeraster(r);
	}
	free(fb);
}

Framebufctl *
_mkfbctl(Rectangle r)
{
	Framebufctl *fc;

	fc = _emalloc(sizeof *fc);
	memset(fc, 0, sizeof *fc);
	fc->fb[0] = _mkfb(r);
	fc->fb[1] = _mkfb(r);
	fc->draw = framebufctl_draw;
	fc->memdraw = framebufctl_memdraw;
	fc->swap = framebufctl_swap;
	fc->reset = framebufctl_reset;
	fc->createraster = framebufctl_createraster;
	fc->fetchraster = framebufctl_fetchraster;
	fc->getfb = framebufctl_getfb;
	fc->getbb = framebufctl_getbb;
	fc->reset(fc);
	return fc;
}

void
_rmfbctl(Framebufctl *fc)
{
	_rmfb(fc->fb[1]);
	_rmfb(fc->fb[0]);
	free(fc);
}
