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
	v->fbctl->draw(v->fbctl, dst);
}

static void
viewport_memdraw(Viewport *v, Memimage *dst)
{
	v->fbctl->memdraw(v->fbctl, dst);
}

static Framebuf *
viewport_getfb(Viewport *v)
{
	return v->fbctl->fb[v->fbctl->idx];	/* front buffer */
}

static Framebuf *
viewport_getbb(Viewport *v)
{
	return v->fbctl->fb[v->fbctl->idx^1];	/* back buffer */
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
	v->draw = viewport_draw;
	v->memdraw = viewport_memdraw;
	v->getfb = viewport_getfb;
	v->getbb = viewport_getbb;
	return v;
}

void
rmviewport(Viewport *v)
{
	rmfbctl(v->fbctl);
	free(v);
}
