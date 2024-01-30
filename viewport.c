#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

Viewport *
mkviewport(Rectangle r)
{
	Viewport *v;

	v = emalloc(sizeof *v);
	v->p = Pt2(0,0,1);
	v->bx = Vec2(1,0);
	v->by = Vec2(0,1);
	v->fbctl = mkfbctl(r);
	return v;
}

void
rmviewport(Viewport *v)
{
	rmfbctl(v->fbctl);
	free(v);
}
