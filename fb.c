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
framebufctl_draw(Framebufctl *ctl, Image *dst, char *name, Viewport *view)
{
	Framebuf *fb;
	Raster *r, *r2;
	Matrix m;
	Warp w;

	qlock(ctl);
	fb = ctl->getfb(ctl);

	r = name == nil? fb->rasters: fb->fetchraster(fb, name);
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

	loadimage(view->drawfb, view->drawfb->r, _rasterbyteaddr(r, fb->r.min), Dx(fb->r)*Dy(fb->r)*4);
	rframematrix(m, *view);
	mkwarp(w, m);
	affinewarp(dst, dst->r, view->drawfb, ZP, w, view->filter);

	qunlock(ctl);
	_freeraster(r2);
}

static void
framebufctl_memdraw(Framebufctl *ctl, Memimage *dst, char *name, Viewport *view)
{
	Framebuf *fb;
	Raster *r, *r2;
	Memimage *tmp;
	uchar *bdata0;
	Matrix m;
	Warp w;

	qlock(ctl);
	fb = ctl->getfb(ctl);

	r = name == nil? fb->rasters: fb->fetchraster(fb, name);
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

	tmp = _eallocmemimage(fb->r, RGBA32);
	bdata0 = tmp->data->bdata;
	tmp->data->bdata = (void*)r->data;
	rframematrix(m, *view);
	mkwarp(w, m);
	memaffinewarp(dst, dst->r, tmp, tmp->r.min, w, view->filter);
	tmp->data->bdata = bdata0;
	freememimage(tmp);
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

static int
framebufctl_createraster(Framebufctl *ctl, char *name, ulong chan)
{
	Framebuf **fb;

	for(fb = ctl->fb; fb < ctl->fb+2; fb++)
		if((*fb)->createraster(*fb, name, chan) < 0)
			return -1;
	return 0;
}

static Raster *
framebufctl_fetchraster(Framebufctl *ctl, char *name)
{
	Framebuf *fb;

	fb = ctl->getfb(ctl);
	return fb->fetchraster(fb, name);
}

static int
fb_createraster(Framebuf *fb, char *name, ulong chan)
{
	Raster **r;

	if(name == nil || name[0] == '\0'){
		werrstr("name can't be empty");
		return -1;
	}

	r = &fb->rasters;
	while(*r != nil){
		if(strcmp((*r)->name, name) == 0){
			werrstr("duplicate name \"%s\"", name);
			return -1;
		}
		r = &(*r)->next;
	}
	*r = _allocraster(name, fb->r, chan);
	return 0;
}

static Raster *
fb_fetchraster(Framebuf *fb, char *name)
{
	Raster *r;

	if(name == nil)
		return nil;

	for(r = fb->rasters; r != nil; r = r->next)
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
	fb->r = r;
	fb->createraster = fb_createraster;
	fb->fetchraster = fb_fetchraster;

	fb->createraster(fb, "color", COLOR32);
	fb->createraster(fb, "depth", FLOAT32);

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
