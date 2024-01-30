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
framebufctl_draw(Framebufctl *ctl, Memimage *dst)
{
	lock(&ctl->swplk);
	memimagedraw(dst, dst->r, ctl->fb[ctl->idx]->cb, ZP, nil, ZP, SoverD);
	unlock(&ctl->swplk);
}

static void
framebufctl_swap(Framebufctl *ctl)
{
	lock(&ctl->swplk);
	ctl->idx ^= 1;
	unlock(&ctl->swplk);
}

static void
framebufctl_reset(Framebufctl *ctl)
{
	Framebuf *fb;

	/* address the back buffer—resetting the front buffer is VERBOTEN */
	fb = ctl->fb[ctl->idx^1];
	memsetd(fb->zbuf, Inf(-1), Dx(fb->r)*Dy(fb->r));
	memfillcolor(fb->cb, DTransparent);
}

Framebuf *
mkfb(Rectangle r)
{
	Framebuf *fb;

	fb = emalloc(sizeof *fb);
	memset(fb, 0, sizeof *fb);
	fb->cb = eallocmemimage(r, RGBA32);
	fb->zbuf = emalloc(Dx(r)*Dy(r)*sizeof(*fb->zbuf));
	memsetd(fb->zbuf, Inf(-1), Dx(r)*Dy(r));
	fb->r = r;
	return fb;
}

void
rmfb(Framebuf *fb)
{
	free(fb->zbuf);
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
	fc->swap = framebufctl_swap;
	fc->reset = framebufctl_reset;
	return fc;
}

void
rmfbctl(Framebufctl *fc)
{
	rmfb(fc->fb[1]);
	rmfb(fc->fb[0]);
	free(fc);
}
