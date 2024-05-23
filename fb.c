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
framebufctl_draw(Framebufctl *ctl, Image *dst)
{
	Framebuf *fb;

	qlock(ctl);
	fb = ctl->getfb(ctl);
	loadimage(dst, rectaddpt(fb->r, dst->r.min), byteaddr(fb->cb, fb->r.min), bytesperline(fb->r, fb->cb->depth)*Dy(fb->r));
	qunlock(ctl);
}

static void
framebufctl_memdraw(Framebufctl *ctl, Memimage *dst)
{
	Framebuf *fb;

	qlock(ctl);
	fb = ctl->getfb(ctl);
	memimagedraw(dst, dst->r, fb->cb, ZP, nil, ZP, SoverD);
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
	memsetd(fb->zb, Inf(-1), Dx(fb->r)*Dy(fb->r));
	memfillcolor(fb->cb, DTransparent);
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
	fb->cb = eallocmemimage(r, RGBA32);
	fb->zb = emalloc(Dx(r)*Dy(r)*sizeof(*fb->zb));
	memsetd(fb->zb, Inf(-1), Dx(r)*Dy(r));
	fb->r = r;
	return fb;
}

void
rmfb(Framebuf *fb)
{
	free(fb->zb);
	freememimage(fb->cb);
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
