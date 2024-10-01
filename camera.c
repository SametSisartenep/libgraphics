#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

/*
 * references:
 * 	- https://learnopengl.com/Advanced-OpenGL/Cubemaps
 */
static Point3
skyboxvs(Shaderparams *sp)
{
	Point3 p;

	sp->setattr(sp, "dir", VAPoint, &sp->v->p);
	/* only rotate along with the camera */
	p = sp->v->p;
	p.w = 0; p = world2vcs(sp->su->camera, p);
	p.w = 1; p = vcs2clip(sp->su->camera, p);
	/* force the cube to always be on the far plane */
	p.z = -p.w;
	return p;
}

static Color
skyboxfs(Shaderparams *sp)
{
	Vertexattr *va;
	Color c;

	va = sp->getattr(sp, "dir");
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

		m->addprim(m, t[0]);
		m->addprim(m, t[1]);
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
verifycfg(Camera *c)
{
	assert(c->view != nil);
	if(c->projtype == PERSPECTIVE)
		assert(c->fov > 0 && c->fov < 360*DEG);
	assert(c->clip.n > 0 && c->clip.n < c->clip.f);
}

Camera *
Camv(Viewport *v, Renderer *r, Projection p, double fov, double n, double f)
{
	Camera *c;

	if(v == nil || r == nil)
		return nil;

	c = newcamera();
	c->view = v;
	c->rctl = r;
	configcamera(c, p, fov, n, f);
	return c;
}

Camera *
Cam(Rectangle vr, Renderer *r, Projection p, double fov, double n, double f)
{
	Viewport *v;

	v = mkviewport(vr);
	if(v == nil){
		werrstr("mkviewport: %r");
		return nil;
	}
	return Camv(v, r, p, fov, n, f);
}

Camera *
newcamera(void)
{
	Camera *c;

	c = emalloc(sizeof *c);
	memset(c, 0, sizeof *c);
	c->rendopts = RODepth;
	return c;
}

void
delcamera(Camera *c)
{
	if(c == nil)
		return;
	rmviewport(c->view);
	free(c);
}

void
reloadcamera(Camera *c)
{
	double a;
	double l, r, b, t;

	verifycfg(c);

	switch(c->projtype){
	case ORTHOGRAPHIC:
		r = Dx(c->view->r)/2;
		t = Dy(c->view->r)/2;
		l = -r;
		b = -t;
		orthographic(c->proj, l, r, b, t, c->clip.n, c->clip.f);
		break;
	case PERSPECTIVE:
		a = (double)Dx(c->view->r)/Dy(c->view->r);
		perspective(c->proj, c->fov, a, c->clip.n, c->clip.f);
		break;
	default: sysfatal("unknown projection type");
	}
}

void
configcamera(Camera *c, Projection p, double fov, double n, double f)
{
	c->projtype = p;
	c->fov = fov;
	c->clip.n = n;
	c->clip.f = f;
	reloadcamera(c);
}

void
placecamera(Camera *c, Scene *s, Point3 p, Point3 focus, Point3 up)
{
	c->scene = s;
	c->p = p;
	c->bz = focus.w == 0? focus: normvec3(subpt3(c->p, focus));
	c->bx = normvec3(crossvec3(up, c->bz));
	c->by = crossvec3(c->bz, c->bx);
}

void
movecamera(Camera *c, Point3 p)
{
	c->p = p.w == 0? addpt3(c->p, p): p;
}

void
rotatecamera(Camera *c, Point3 axis, double θ)
{
	c->bx = qrotate(c->bx, axis, θ);
	c->by = qrotate(c->by, axis, θ);
	c->bz = qrotate(c->bz, axis, θ);
}

void
aimcamera(Camera *c, Point3 focus)
{
	c->bz = focus.w == 0? focus: normvec3(subpt3(c->p, focus));
	c->bx = normvec3(crossvec3(c->by, c->bz));
	c->by = crossvec3(c->bz, c->bx);
}

static void
printtimings(Renderjob *job)
{
	int i;

	if(!job->rctl->doprof)
		return;

	fprint(2, "R %llud %llud\nE %llud %llud\n",
		job->times.R.t0, job->times.R.t1,
		job->times.E.t0, job->times.E.t1);
	for(i = 0; i < job->rctl->nprocs/2; i++)
		fprint(2, "T%d %llud %llud\n", i,
			job->times.Tn[i].t0, job->times.Tn[i].t1);
	for(i = 0; i < job->rctl->nprocs/2; i++)
		fprint(2, "r%d %llud %llud\n", i,
			job->times.Rn[i].t0, job->times.Rn[i].t1);
	fprint(2, "\n");
}

void
shootcamera(Camera *c, Shadertab *s)
{
	static Scene *skyboxscene;
	static Shadertab skyboxshader = { nil, skyboxvs, skyboxfs };
	Model *mdl;
	Framebufctl *fbctl;
	Renderjob *job;
	uvlong t0, t1;

	assert(c->view != nil && c->rctl != nil && c->scene != nil && s != nil);

	fbctl = c->view->fbctl;

	job = emalloc(sizeof *job);
	memset(job, 0, sizeof *job);
	job->rctl = c->rctl;
	job->fb = fbctl->getbb(fbctl);
	job->camera = emalloc(sizeof *c);
	*job->camera = *c;
	job->scene = dupscene(c->scene);	/* take a snapshot */	
	job->shaders = s;
	job->donec = chancreate(sizeof(void*), 0);

	fbctl->reset(fbctl);
	t0 = nanosec();
	sendp(c->rctl->jobq, job);
	recvp(job->donec);
	delscene(job->scene);			/* destroy the snapshot */
	/*
	 * if the scene has a skybox, do another render pass,
	 * filling in the pixels left untouched.
	 */
	if(c->scene->skybox != nil){
		if(skyboxscene == nil){
			skyboxscene = newscene("skybox");
			mdl = mkskyboxmodel();
			skyboxscene->addent(skyboxscene, newentity("skybox", mdl));
		}
		job->camera->cullmode = CullNone;
		job->camera->fov = 90*DEG;
		reloadcamera(job->camera);
		job->scene = dupscene(skyboxscene);
		job->shaders = &skyboxshader;
		sendp(c->rctl->jobq, job);
		recvp(job->donec);
		delscene(job->scene);
	}
	t1 = nanosec();
	fbctl->swap(fbctl);

	updatestats(c, t1-t0);
	printtimings(job);
	free(job->times.Tn);
	free(job->times.Rn);

	free(job->cliprects);
	chanfree(job->donec);
	free(job->camera);
	free(job);
}
