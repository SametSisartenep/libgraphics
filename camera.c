#include <u.h>
#include <libc.h>
#include <draw.h>
#include <geometry.h>
#include <graphics.h>

static int
max(int a, int b)
{
	return a > b ? a : b;
}

static void
verifycfg(Camera *c)
{
	assert(c->viewport != nil);
	if(c->ptype == Ppersp)
		assert(c->fov > 0 && c->fov < 360);
	assert(c->clip.n > 0 && c->clip.n < c->clip.f);
}

void
configcamera(Camera *c, Image *v, double fov, double n, double f, Projection p)
{
	c->viewport = v;
	c->fov = fov;
	c->clip.n = n;
	c->clip.f = f;
	c->ptype = p;
	reloadcamera(c);
}

void
placecamera(Camera *c, Point3 p, Point3 focus, Point3 up)
{
	c->p = p;
	if(focus.w == 0)
		c->bz = focus;
	else
		c->bz = normvec3(subpt3(c->p, focus));
	c->bx = normvec3(crossvec3(up, c->bz));
	c->by = crossvec3(c->bz, c->bx);
}

void
aimcamera(Camera *c, Point3 focus)
{
	placecamera(c, c->p, focus, c->by);
}

void
reloadcamera(Camera *c)
{
	double a;
	double l, r, b, t;

	verifycfg(c);
	switch(c->ptype){
	case Portho:
		/*
		r = Dx(c->viewport->r)/2;
		t = Dy(c->viewport->r)/2;
		l = -r;
		b = -t;
		*/
		l = t = 0;
		r = Dx(c->viewport->r);
		b = Dy(c->viewport->r);
		orthographic(c->proj, l, r, b, t, c->clip.n, c->clip.f);
		break;
	case Ppersp:
		a = (double)Dx(c->viewport->r)/Dy(c->viewport->r);
		perspective(c->proj, c->fov, a, c->clip.n, c->clip.f);
		break;
	default: sysfatal("unknown projection type");
	}
}
