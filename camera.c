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
 * references:
 * 	- https://learnopengl.com/Advanced-OpenGL/Cubemaps
 */
static Point3
skyboxvs(VSparams *sp)
{
	Point3 p;

	addvattr(sp->v, "dir", VAPoint, &sp->v->p);
	/* only rotate along with the camera */
	p = sp->v->p; p.w = 0;
	p = world2vcs(sp->su->camera, p); p.w = 1;
	p = vcs2clip(sp->su->camera, p);
	/* force the cube to always be on the far plane */
	p.z = -p.w;
	return p;
}

static Color
skyboxfs(FSparams *sp)
{
	Vertexattr *va;
	Color c;

	va = getvattr(&sp->v, "dir");
	c = samplecubemap(sp->su->camera->scene->skybox, va->p, neartexsampler);
	return c;
}

static Model *
mkskyboxmodel(void)
{
	static Point3 axes[3] = {{0,1,0,0}, {1,0,0,0}, {0,0,1,0}};
	static Point3 center = {0,0,0,1};
	Model *m;
	Primitive t[2];
	Point3 p, v1, v2;
	int i, j, k;

	m = newmodel();
	memset(t, 0, sizeof t);
	t[0].type = t[1].type = PTriangle;

	p = Vec3(-0.5,-0.5,0.5);
	v1 = Vec3(1,0,0);
	v2 = Vec3(0,1,0);
	t[0].v[0].p = addpt3(center, p);
	t[0].v[1].p = addpt3(center, addpt3(p, v1));
	t[0].v[2].p = addpt3(center, addpt3(p, addpt3(v1, v2)));
	t[1].v[0] = t[0].v[0];
	t[1].v[1] = t[0].v[2];
	t[1].v[2].p = addpt3(center, addpt3(p, v2));

	for(i = 0; i < 6; i++){
		for(j = 0; j < 2; j++)
			for(k = 0; k < 3; k++)
				if(i > 0)
					t[j].v[k].p = qrotate(t[j].v[k].p, axes[i%3], PI/2);

		m->prims = erealloc(m->prims, (m->nprims += 2)*sizeof(*m->prims));
		m->prims[m->nprims-2] = t[0];
		m->prims[m->nprims-1] = t[1];
	}
	return m;
}

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
updatetimes(Camera *c, Renderjob *j)
{
	c->times.R[c->times.cur] = j->times.R;
	c->times.E[c->times.cur] = j->times.E;
	c->times.Tn[c->times.cur] = j->times.Tn;
	c->times.Rn[c->times.cur] = j->times.Rn;
	c->times.cur = ++c->times.cur % nelem(c->times.R);
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
	static Scene *skyboxscene;
	static Shadertab skyboxshader = { nil, skyboxvs, skyboxfs };
	Model *mdl;
	Renderjob *job;
	uvlong t0, t1;

	job = emalloc(sizeof *job);
	memset(job, 0, sizeof *job);
	job->fb = c->vp->fbctl->getbb(c->vp->fbctl);
	job->camera = c;
	job->scene = c->scene;
	job->shaders = s;
	job->donec = chancreate(sizeof(void*), 0);

	c->vp->fbctl->reset(c->vp->fbctl);
	t0 = nanosec();
	sendp(c->rctl->c, job);
	recvp(job->donec);
	/*
	 * if the scene has a skybox, do another render pass,
	 * filling in the fragments left untouched by the z-buffer.
	 */
	if(c->scene->skybox != nil){
		if(skyboxscene == nil){
			skyboxscene = newscene("skybox");
			mdl = mkskyboxmodel();
			skyboxscene->addent(skyboxscene, newentity(mdl));
		}
		skyboxscene->skybox = c->scene->skybox;
		job->scene = skyboxscene;
		job->shaders = &skyboxshader;
		sendp(c->rctl->c, job);
		recvp(job->donec);
	}
	t1 = nanosec();
	c->vp->fbctl->swap(c->vp->fbctl);

	updatestats(c, t1-t0);
	updatetimes(c, job);

	chanfree(job->donec);
	free(job);
}
