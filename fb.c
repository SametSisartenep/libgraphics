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
scale2x_filter(ulong *dst, Raster *fb, Point sp, ulong)
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
scale3x_filter(ulong *dst, Raster *fb, Point sp, ulong)
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
//scale4x_filter(ulong *dst, Raster *fb, Point sp, ulong)
//{
//
//}

static void
ident_filter(ulong *dst, Raster *fb, Point sp, ulong len)
{
	memsetl(dst, getpixel(fb, sp), len);
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
	for(min = 0, max = 0; len--; f++){
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
premulalpha(Raster *r)
{
	Color c;
	ulong *p, len;

	len = Dx(r->r)*Dy(r->r);
	p = r->data;
	while(len--){
		c = ul2col(*p);
		c.r *= c.a;
		c.g *= c.a;
		c.b *= c.a;
		*p++ = col2ul(c);
	}
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
	r->next = allocraster(name, fb->r, chan);
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

static void
upscaledraw(Raster *fb, Image *dst, Point off, Point scale, uint filter)
{
	void (*filterfn)(ulong*, Raster*, Point, ulong);
	Rectangle blkr;
	Point sp, dp;
	Image *tmp;
	ulong *blk;

	filterfn = nil;
	blk = emalloc(scale.x*scale.y*4);
	blkr = Rect(0,0,scale.x,scale.y);
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
	default: filterfn = ident_filter;
	}

	for(sp.y = fb->r.min.y, dp.y = dst->r.min.y+off.y; sp.y < fb->r.max.y; sp.y++, dp.y += scale.y)
	for(sp.x = fb->r.min.x, dp.x = dst->r.min.x+off.x; sp.x < fb->r.max.x; sp.x++, dp.x += scale.x){
		filterfn(blk, fb, sp, scale.x*scale.y);
		loadimage(tmp, rectaddpt(blkr, dp), (uchar*)blk, scale.x*scale.y*4);
	}
	draw(dst, dst->r, tmp, nil, tmp->r.min);
	freeimage(tmp);
	free(blk);
}

static void
framebufctl_draw(Framebufctl *ctl, Image *dst, char *name, Point off, Point scale)
{
	Framebuf *fb;
	Raster *r, *r2;
	Rectangle sr, dr;
	Image *tmp;

	qlock(ctl);
	fb = ctl->getfb(ctl);

	r = fb->fetchraster(fb, name);
	if(r == nil){
		qunlock(ctl);
		return;
	}

	r2 = nil;
	if(r->chan == FLOAT32){
		r2 = allocraster(nil, r->r, COLOR32);
		rasterconvF2C(r2, r);
		r = r2;
	}

	/* this means the raster is a color one, so duplicate it */
	if(r2 == nil){
		r2 = allocraster(nil, r->r, COLOR32);
		memmove(r2->data, r->data, Dx(r->r)*Dy(r->r)*4);
		r = r2;
	}
	premulalpha(r);

	if(scale.x > 1 || scale.y > 1){
		upscaledraw(r, dst, off, scale, ctl->upfilter);
		qunlock(ctl);
		freeraster(r2);
		return;
	}

	/* TODO use the clipr in upscaledraw too */
	sr = rectaddpt(fb->clipr, off);
	dr = rectsubpt(dst->r, dst->r.min);
	if(rectclip(&sr, dr)){
		tmp = allocimage(display, sr, RGBA32, 0, DNofill);
		if(tmp == nil)
			sysfatal("allocimage: %r");

		dr = sr;
		dr.max.y = dr.min.y + 1;
		/* remove offset to get the actual rect within the framebuffer */
		sr = rectsubpt(sr, off);
		for(; sr.min.y < sr.max.y; sr.min.y++, dr.min.y++, dr.max.y++)
			loadimage(tmp, rectaddpt(dr, dst->r.min), rasterbyteaddr(r, sr.min), Dx(dr)*4);
		draw(dst, rectaddpt(tmp->r, dst->r.min), tmp, nil, tmp->r.min);
		freeimage(tmp);
	}
	qunlock(ctl);
	freeraster(r2);
}

static void
upscalememdraw(Raster *fb, Memimage *dst, Point off, Point scale, uint filter)
{
	void (*filterfn)(ulong*, Raster*, Point, ulong);
	Rectangle blkr;
	Point sp, dp;
	Memimage *tmp;
	ulong *blk;

	filterfn = nil;
	blk = emalloc(scale.x*scale.y*4);
	blkr = Rect(0,0,scale.x,scale.y);
	tmp = allocmemimage(dst->r, RGBA32);
	if(tmp == nil)
		sysfatal("allocmemimage: %r");

	switch(filter){
	case UFScale2x:
		if(scale.x == scale.y && scale.y == 2)
			filterfn = scale2x_filter;
		break;
	case UFScale3x:
		if(scale.x == scale.y && scale.y == 3)
			filterfn = scale3x_filter;
		break;
	default: filterfn = ident_filter;
	}

	for(sp.y = fb->r.min.y, dp.y = dst->r.min.y+off.y; sp.y < fb->r.max.y; sp.y++, dp.y += scale.y)
	for(sp.x = fb->r.min.x, dp.x = dst->r.min.x+off.x; sp.x < fb->r.max.x; sp.x++, dp.x += scale.x){
		filterfn(blk, fb, sp, scale.x*scale.y);
		loadmemimage(tmp, rectaddpt(blkr, dp), (uchar*)blk, scale.x*scale.y*4);
	}
	memimagedraw(dst, dst->r, tmp, tmp->r.min, nil, ZP, S);
	freememimage(tmp);
	free(blk);
}

static void
framebufctl_memdraw(Framebufctl *ctl, Memimage *dst, char *name, Point off, Point scale)
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
		r2 = allocraster(nil, r->r, COLOR32);
		rasterconvF2C(r2, r);
		r = r2;
	}

	/* this means the raster is a color one, so duplicate it */
	if(r2 == nil){
		r2 = allocraster(nil, r->r, COLOR32);
		memmove(r2->data, r->data, Dx(r->r)*Dy(r->r)*4);
		r = r2;
	}
	premulalpha(r);

	if(scale.x > 1 || scale.y > 1){
		upscalememdraw(r, dst, off, scale, ctl->upfilter);
		qunlock(ctl);
		freeraster(r2);
		return;
	}

	sr = rectaddpt(fb->r, off);
	dr = rectsubpt(dst->r, dst->r.min);
	if(rectinrect(sr, dr)){
		tmp = allocmemimage(sr, RGBA32);
		if(tmp == nil)
			sysfatal("allocmemimage: %r");

		bdata0 = tmp->data->bdata;
		tmp->data->bdata = (void*)r->data;
		memimagedraw(dst, rectaddpt(sr, dst->r.min), tmp, ZP, nil, ZP, S);
		tmp->data->bdata = bdata0;
		freememimage(tmp);
	}else if(rectclip(&sr, dr)){
		tmp = allocmemimage(sr, RGBA32);
		if(tmp == nil)
			sysfatal("allocmemimage: %r");

		dr = sr;
		dr.max.y = dr.min.y + 1;
		/* remove offset to get the actual rect within the framebuffer */
		sr = rectsubpt(sr, off);
		for(; sr.min.y < sr.max.y; sr.min.y++, dr.min.y++, dr.max.y++)
			loadmemimage(tmp, rectaddpt(dr, dst->r.min), rasterbyteaddr(r, sr.min), Dx(dr)*4);
		memimagedraw(dst, rectaddpt(tmp->r, dst->r.min), tmp, tmp->r.min, nil, ZP, S);
		freememimage(tmp);
	}
	qunlock(ctl);
	freeraster(r2);
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
	clearraster(r, 0);
	r = r->next;			/* z-buffer */
	fclearraster(r, Inf(-1));
	while((r = r->next) != nil)
		clearraster(r, 0);	/* every other raster */
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
	Framebuf *fb;
	int i;

	for(i = 0; i < 2; i++){
		fb = ctl->fb[i];
		fb->createraster(fb, name, chan);
	}
}

static Raster *
framebufctl_fetchraster(Framebufctl *ctl, char *name)
{
	Framebuf *fb;

	fb = ctl->getfb(ctl);
	return fb->fetchraster(fb, name);
}

Raster *
allocraster(char *name, Rectangle rr, ulong chan)
{
	Raster *r;

	assert(chan <= FLOAT32);

	r = emalloc(sizeof *r);
	memset(r, 0, sizeof *r);
	if(name != nil && (r->name = strdup(name)) == nil)
		sysfatal("strdup: %r");
	r->chan = chan;
	r->r = rr;
	r->data = emalloc(Dx(rr)*Dy(rr)*sizeof(*r->data));
	return r;
}

void
clearraster(Raster *r, ulong v)
{
	memsetl(r->data, v, Dx(r->r)*Dy(r->r));
}

void
fclearraster(Raster *r, float v)
{
	memsetf(r->data, v, Dx(r->r)*Dy(r->r));
}

uchar *
rasterbyteaddr(Raster *r, Point p)
{
	return (uchar*)&r->data[p.y*Dx(r->r) + p.x];
}

void
rasterput(Raster *r, Point p, void *v)
{
	switch(r->chan){
	case COLOR32:
		*(ulong*)rasterbyteaddr(r, p) = *(ulong*)v;
		break;
	case FLOAT32:
		*(float*)rasterbyteaddr(r, p) = *(float*)v;
		break;
	}
}

void
rasterget(Raster *r, Point p, void *v)
{
	switch(r->chan){
	case COLOR32:
		*(ulong*)v = *(ulong*)rasterbyteaddr(r, p);
		break;
	case FLOAT32:
		*(float*)v = *(float*)rasterbyteaddr(r, p);
		break;
	}
}

void
rasterputcolor(Raster *r, Point p, ulong c)
{
	rasterput(r, p, &c);
}

ulong
rastergetcolor(Raster *r, Point p)
{
	ulong c;

	rasterget(r, p, &c);
	return c;
}

void
rasterputfloat(Raster *r, Point p, float v)
{
	rasterput(r, p, &v);
}

float
rastergetfloat(Raster *r, Point p)
{
	float v;

	rasterget(r, p, &v);
	return v;
}

void
freeraster(Raster *r)
{
	if(r == nil)
		return;
	free(r->data);
	free(r->name);
	free(r);
}

Framebuf *
mkfb(Rectangle r)
{
	Framebuf *fb;

	fb = emalloc(sizeof *fb);
	memset(fb, 0, sizeof *fb);
	fb->rasters = allocraster(nil, r, COLOR32);
	fb->rasters->next = allocraster("z-buffer", r, FLOAT32);
	fb->r = r;
	fb->createraster = fb_createraster;
	fb->fetchraster = fb_fetchraster;
	return fb;
}

void
rmfb(Framebuf *fb)
{
	Raster *r, *nr;

	for(r = fb->rasters; r != nil; r = nr){
		nr = r->next;
		freeraster(r);
	}
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
	fc->memdraw = framebufctl_memdraw;
	fc->swap = framebufctl_swap;
	fc->reset = framebufctl_reset;
	fc->createraster = framebufctl_createraster;
	fc->fetchraster = framebufctl_fetchraster;
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
