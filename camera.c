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
updatestats(Camera *c, uvlong v)
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
verifycfg(Camera *c)
{
	assert(c->vp != nil);
	if(c->projtype == PERSPECTIVE)
		assert(c->fov > 0 && c->fov < 360*DEG);
	assert(c->clip.n > 0 && c->clip.n < c->clip.f);
}

void
configcamera(Camera *c, Viewport *v, double fov, double n, double f, Projection p)
{
	c->vp = v;
	c->fov = fov;
	c->clip.n = n;
	c->clip.f = f;
	c->projtype = p;
	reloadcamera(c);
}

void
placecamera(Camera *c, Point3 p, Point3 focus, Point3 up)
{
	c->p = p;
	c->bz = focus.w == 0? focus: normvec3(subpt3(c->p, focus));
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
	switch(c->projtype){
	case ORTHOGRAPHIC:
		r = Dx(c->vp->fbctl->fb[0]->r)/2;
		t = Dy(c->vp->fbctl->fb[0]->r)/2;
		l = -r;
		b = -t;
		orthographic(c->proj, l, r, b, t, c->clip.n, c->clip.f);
		break;
	case PERSPECTIVE:
		a = (double)Dx(c->vp->fbctl->fb[0]->r)/Dy(c->vp->fbctl->fb[0]->r);
		perspective(c->proj, c->fov, a, c->clip.n, c->clip.f);
		break;
	default: sysfatal("unknown projection type");
	}
}

void
shootcamera(Camera *c, Shadertab *s)
{
	Renderjob *job;
	uvlong t0, t1;

	job = emalloc(sizeof *job);
	memset(job, 0, sizeof *job);
	job->fb = c->vp->fbctl->fb[c->vp->fbctl->idx^1];	/* address the back buffer */
	job->scene = c->s;
	job->shaders = s;
	job->donec = chancreate(sizeof(void*), 0);

	c->vp->fbctl->reset(c->vp->fbctl);
	t0 = nanosec();
	sendp(c->rctl->c, job);
	recvp(job->donec);
	t1 = nanosec();
	c->vp->fbctl->swap(c->vp->fbctl);

	chanfree(job->donec);
	free(job);

	updatestats(c, t1-t0);
}
