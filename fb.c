#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

/*
 * scale[234]x filters
 *
 * see https://www.scale2x.it/algorithm
 */
static void
scale2x_filter(ulong *dst, Framebuf *fb, Point sp)
{
	ulong B, D, E, F, H;

	E = getpixel(fb, sp);
	B = sp.y == fb->r.min.y? E: getpixel(fb, addpt(sp, Pt( 0,-1)));
	D = sp.x == fb->r.min.x? E: getpixel(fb, addpt(sp, Pt(-1, 0)));
	F = sp.x == fb->r.max.x? E: getpixel(fb, addpt(sp, Pt( 1, 0)));
	H = sp.y == fb->r.max.y? E: getpixel(fb, addpt(sp, Pt( 0, 1)));

	if(B != H && D != F){
		dst[0] = D == B? D: E;
		dst[1] = B == F? F: E;
		dst[2] = D == H? D: E;
		dst[3] = H == F? F: E;
	}else
		memsetl(dst, E, 4);
}

static void
scale3x_filter(ulong *dst, Framebuf *fb, Point sp)
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
		dst[0] = D == B? D: E;
		dst[1] = (D == B && E != C) || (B == F && E != A)? B: E;
		dst[2] = B == F? F: E;
		dst[3] = (D == B && E != G) || (D == H && E != A)? D: E;
		dst[4] = E;
		dst[5] = (B == F && E != I) || (H == F && E != C)? F: E;
		dst[6] = D == H? D: E;
		dst[7] = (D == H && E != I) || (H == F && E != G)? H: E;
		dst[8] = H == F? F: E;
	}else
		memsetl(dst, E, 9);
}

//static void
//scale4x_filter(ulong *dst, Framebuf *fb, Point sp)
//{
//
//}

//static void
//framebufctl_draw⁻¹(Framebufctl *ctl, Image *dst)
//{
//	Framebuf *fb;
//	Rectangle lr;
//	Point sp, dp;
//
//	qlock(ctl);
//	fb = ctl->getfb(ctl);
//	lr = Rect(0,0,Dx(fb->r),1);
//	sp.x = dp.x = 0;
//	for(sp.y = fb->r.max.y, dp.y = dst->r.min.y; sp.y >= fb->r.min.y; sp.y--, dp.y++)
//		loadimage(dst, rectaddpt(lr, dp), (uchar*)(fb->cb + sp.y*Dx(lr)), Dx(lr)*4);
//	qunlock(ctl);
//}

static void
framebufctl_draw(Framebufctl *ctl, Image *dst, Point off)
{
	Framebuf *fb;

	qlock(ctl);
	fb = ctl->getfb(ctl);
	loadimage(dst, rectaddpt(fb->r, addpt(dst->r.min, off)), (uchar*)fb->cb, Dx(fb->r)*Dy(fb->r)*4);
	qunlock(ctl);
}

static void
framebufctl_upscaledraw(Framebufctl *ctl, Image *dst, Point off, Point scale)
{
	void (*filter)(ulong*, Framebuf*, Point);
	Framebuf *fb;
	Rectangle blkr;
	Point sp, dp;
	ulong *blk;

	filter = nil;
	blk = emalloc(scale.x*scale.y*4);
	blkr = Rect(0,0,scale.x,scale.y);

	qlock(ctl);
	fb = ctl->getfb(ctl);

	switch(ctl->upfilter){
	case UFScale2x:
		if(scale.x == scale.y && scale.y == 2)
			filter = scale2x_filter;
		break;
	case UFScale3x:
		if(scale.x == scale.y && scale.y == 3)
			filter = scale3x_filter;
		break;
	}

	for(sp.y = fb->r.min.y, dp.y = dst->r.min.y+off.y; sp.y < fb->r.max.y; sp.y++, dp.y += scale.y)
	for(sp.x = fb->r.min.x, dp.x = dst->r.min.x+off.x; sp.x < fb->r.max.x; sp.x++, dp.x += scale.x){
		if(filter != nil)
			filter(blk, fb, sp);
		else
			memsetl(blk, getpixel(fb, sp), scale.x*scale.y);
		loadimage(dst, rectaddpt(blkr, dp), (uchar*)blk, scale.x*scale.y*4);
	}
	qunlock(ctl);
	free(blk);
}

static void
framebufctl_memdraw(Framebufctl *ctl, Memimage *dst, Point off)
{
	Framebuf *fb;

	qlock(ctl);
	fb = ctl->getfb(ctl);
	loadmemimage(dst, rectaddpt(fb->r, addpt(dst->r.min, off)), (uchar*)fb->cb, Dx(fb->r)*Dy(fb->r)*4);
	qunlock(ctl);
}

static void
framebufctl_upscalememdraw(Framebufctl *ctl, Memimage *dst, Point off, Point scale)
{
	void (*filter)(ulong*, Framebuf*, Point);
	Framebuf *fb;
	Rectangle blkr;
	Point sp, dp;
	ulong *blk;

	filter = nil;
	blk = emalloc(scale.x*scale.y*4);
	blkr = Rect(0,0,scale.x,scale.y);

	qlock(ctl);
	fb = ctl->getfb(ctl);

	switch(ctl->upfilter){
	case UFScale2x:
		if(scale.x == scale.y && scale.y == 2)
			filter = scale2x_filter;
		break;
	case UFScale3x:
		if(scale.x == scale.y && scale.y == 3)
			filter = scale3x_filter;
		break;
	}

	for(sp.y = fb->r.min.y, dp.y = dst->r.min.y+off.y; sp.y < fb->r.max.y; sp.y++, dp.y += scale.y)
	for(sp.x = fb->r.min.x, dp.x = dst->r.min.x+off.x; sp.x < fb->r.max.x; sp.x++, dp.x += scale.x){
		if(filter != nil)
			filter(blk, fb, sp);
		else
			memsetl(blk, getpixel(fb, sp), scale.x*scale.y);
		loadmemimage(dst, rectaddpt(blkr, dp), (uchar*)blk, scale.x*scale.y*4);
	}
	qunlock(ctl);
	free(blk);
}

static void
framebufctl_drawnormals(Framebufctl *ctl, Image *dst)
{
	Framebuf *fb;

	qlock(ctl);
	fb = ctl->getfb(ctl);
	loadimage(dst, rectaddpt(fb->r, dst->r.min), (uchar*)fb->nb, Dx(fb->r)*Dy(fb->r)*4);
	qunlock(ctl);
}

static void
framebufctl_swap(Framebufctl *ctl)
{
	qlock(ctl);
	ctl->idx ^= 1;
	qunlock(ctl);
}

static void
framebufctl_reset(Framebufctl *ctl)
{
	Framebuf *fb;

	/* address the back buffer—resetting the front buffer is VERBOTEN */
	fb = ctl->getbb(ctl);
	memset(fb->nb, 0, Dx(fb->r)*Dy(fb->r)*4);
	memsetf(fb->zb, Inf(-1), Dx(fb->r)*Dy(fb->r));
	memset(fb->cb, 0, Dx(fb->r)*Dy(fb->r)*4);
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

Framebuf *
mkfb(Rectangle r)
{
	Framebuf *fb;

	fb = emalloc(sizeof *fb);
	memset(fb, 0, sizeof *fb);
	fb->cb = emalloc(Dx(r)*Dy(r)*4);
	fb->zb = emalloc(Dx(r)*Dy(r)*sizeof(*fb->zb));
	memsetf(fb->zb, Inf(-1), Dx(r)*Dy(r));
	fb->nb = emalloc(Dx(r)*Dy(r)*4);
	fb->r = r;
	return fb;
}

void
rmfb(Framebuf *fb)
{
	free(fb->nb);
	free(fb->zb);
	free(fb->cb);
	free(fb);
}

Framebufctl *
mkfbctl(Rectangle r)
{
	Framebufctl *fc;

	fc = emalloc(sizeof *fc);
	memset(fc, 0, sizeof *fc);
	fc->fb[0] = mkfb(r);
	fc->fb[1] = mkfb(r);
	fc->draw = framebufctl_draw;
	fc->upscaledraw = framebufctl_upscaledraw;
	fc->memdraw = framebufctl_memdraw;
	fc->upscalememdraw = framebufctl_upscalememdraw;
	fc->drawnormals = framebufctl_drawnormals;
	fc->swap = framebufctl_swap;
	fc->reset = framebufctl_reset;
	fc->getfb = framebufctl_getfb;
	fc->getbb = framebufctl_getbb;
	return fc;
}

void
rmfbctl(Framebufctl *fc)
{
	rmfb(fc->fb[1]);
	rmfb(fc->fb[0]);
	free(fc);
}
