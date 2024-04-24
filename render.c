#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

Rectangle UR = {0,0,1,1};

static ulong
col2ul(Color c)
{
	uchar cbuf[4];

	cbuf[0] = c.a*0xFF;
	cbuf[1] = c.b*0xFF;
	cbuf[2] = c.g*0xFF;
	cbuf[3] = c.r*0xFF;
	return cbuf[3]<<24 | cbuf[2]<<16 | cbuf[1]<<8 | cbuf[0];
}

static void
pixel(Memimage *dst, Point p, Memimage *src)
{
	if(dst == nil || src == nil)
		return;

	memimagedraw(dst, rectaddpt(UR, p), src, ZP, nil, ZP, SoverD);
}

static int
isvisible(Point3 p)
{
	if(p.x < -p.w || p.x > p.w ||
	   p.y < -p.w || p.y > p.w ||
	   p.z < -p.w || p.z > p.w)
		return 0;
	return 1;
}

static int
isfacingback(Primitive p)
{
	double sa;	/* signed area */

	sa = p.v[0].p.x * p.v[1].p.y - p.v[0].p.y * p.v[1].p.x +
	     p.v[1].p.x * p.v[2].p.y - p.v[1].p.y * p.v[2].p.x +
	     p.v[2].p.x * p.v[0].p.y - p.v[2].p.y * p.v[0].p.x;
	return sa <= 0;
}

static void
mulsdm(double r[6], double m[6][4], Point3 p)
{
	int i;

	for(i = 0; i < 6; i++)
		r[i] = m[i][0]*p.x + m[i][1]*p.y + m[i][2]*p.z + m[i][3]*p.w;
}

typedef struct
{
	Vertex *v;
	ulong n;
	ulong cap;
} Polygon;

static int
addvert(Polygon *p, Vertex v)
{
	if(++p->n > p->cap)
		p->v = erealloc(p->v, (p->cap = p->n)*sizeof(*p->v));
	p->v[p->n-1] = v;
	return p->n;
}

static void
swappoly(Polygon *a, Polygon *b)
{
	Polygon tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static void
cleanpoly(Polygon *p)
{
	int i;

	for(i = 0; i < p->n; i++)
		delvattrs(&p->v[i]);
	p->n = 0;
}

/*
 * references:
 * 	- James F. Blinn, Martin E. Newell, “Clipping Using Homogeneous Coordinates”,
 * 	  SIGGRAPH '78, pp. 245-251
 * 	- https://cs418.cs.illinois.edu/website/text/clipping.html
 * 	- https://github.com/aap/librw/blob/14dab85dcae6f3762fb2b1eda4d58d8e67541330/tools/playground/tl_tests.cpp#L522
 */
static int
cliptriangle(Primitive *p)
{
	/* signed distance from each clipping plane */
	static double sdm[6][4] = {
		 1,  0,  0, 1,	/* l */
		-1,  0,  0, 1,	/* r */
		 0,  1,  0, 1,	/* b */
		 0, -1,  0, 1,	/* t */
		 0,  0,  1, 1,	/* f */
		 0,  0, -1, 1,	/* n */
	};
	double sd0[6], sd1[6];
	double d0, d1, perc;
	Polygon Vin, Vout;
	Vertex *v0, *v1, v;	/* edge verts and new vertex (line-plane intersection) */
	int i, j, nt;

	nt = 0;
	memset(&Vin, 0, sizeof Vin);
	memset(&Vout, 0, sizeof Vout);
	for(i = 0; i < 3; i++)
		addvert(&Vin, p[0].v[i]);

	for(j = 0; j < 6 && Vin.n > 0; j++){
		for(i = 0; i < Vin.n; i++){
			v0 = &Vin.v[i];
			v1 = &Vin.v[(i+1) % Vin.n];

			mulsdm(sd0, sdm, v0->p);
			mulsdm(sd1, sdm, v1->p);

			if(sd0[j] < 0 && sd1[j] < 0)
				continue;

			if(sd0[j] >= 0 && sd1[j] >= 0)
				goto allin;

			d0 = (j&1) == 0? sd0[j]: -sd0[j];
			d1 = (j&1) == 0? sd1[j]: -sd1[j];
			perc = d0/(d0 - d1);

			lerpvertex(&v, v0, v1, perc);
			addvert(&Vout, v);

			if(sd1[j] >= 0){
allin:
				addvert(&Vout, dupvertex(v1));
			}
		}
		cleanpoly(&Vin);
		if(j < 6-1)
			swappoly(&Vin, &Vout);
	}

	/* triangulate */
	if(Vout.n < 3)
		cleanpoly(&Vout);
	else
		for(i = 0; i < Vout.n-2; i++, nt++){
			/*
			 * when performing fan triangulation, indices 0 and 2
			 * are referenced on every triangle, so duplicate them
			 * to avoid complications during rasterization.
			 */
			p[nt].v[0] = i < Vout.n-2-1? dupvertex(&Vout.v[0]): Vout.v[0];
			p[nt].v[1] = Vout.v[i+1];
			p[nt].v[2] = i < Vout.n-2-1? dupvertex(&Vout.v[i+2]): Vout.v[i+2];
		}
	free(Vout.v);
	free(Vin.v);

	return nt;
}

/*
 * transforms p from e's reference frame into
 * the world.
 */
Point3
model2world(Entity *e, Point3 p)
{
	return invrframexform3(p, *e);
}

/*
 * transforms p from the world reference frame
 * to c's one (aka Viewing Coordinate System).
 */
Point3
world2vcs(Camera *c, Point3 p)
{
	return rframexform3(p, *c);
}

/*
 * projects p from the VCS to clip space, placing
 * p.[xyz] ∈ (-∞,-w)∪[-w,w]∪(w,∞) where [-w,w]
 * represents the visibility volume.
 *
 * the clipping planes are:
 *
 * 	|   -w   |   w   |
 *	+----------------+
 * 	| left   | right |
 * 	| bottom | top   |
 * 	| far    | near  |
 */
Point3
vcs2clip(Camera *c, Point3 p)
{
	return xform3(p, c->proj);
}

Point3
world2clip(Camera *c, Point3 p)
{
	return vcs2clip(c, world2vcs(c, p));
}

/*
 * performs the perspective division, placing
 * p.[xyz] ∈ [-1,1] and p.w = 1/z
 * (aka Normalized Device Coordinates).
 *
 * p.w is kept as z⁻¹ so we can later do
 * perspective-correct attribute interpolation.
 */
static Point3
clip2ndc(Point3 p)
{
	p.w = p.w == 0? 1: 1.0/p.w;
	p.x *= p.w;
	p.y *= p.w;
	p.z *= p.w;
	return p;
}

/*
 * scales p to fit the destination viewport,
 * placing p.x ∈ [0,width], p.y ∈ [0,height],
 * p.z ∈ [0,1] and leaving p.w intact.
 */
static Point3
ndc2viewport(Framebuf *fb, Point3 p)
{
	Matrix3 view = {
		Dx(fb->r)/2.0,             0,       0,       Dx(fb->r)/2.0,
		0,            -Dy(fb->r)/2.0,       0,       Dy(fb->r)/2.0,
		0,                         0, 1.0/2.0,             1.0/2.0,
		0,                         0,       0,                   1,
	};
	double w;

	w = p.w;
	p.w = 1;
	p = xform3(p, view);
	p.w = w;
	return p;
}

void
perspective(Matrix3 m, double fov, double a, double n, double f)
{
	double cotan;

	cotan = 1/tan(fov/2);
	identity3(m);
	m[0][0] =  cotan/a;
	m[1][1] =  cotan;
	m[2][2] =  (f+n)/(f-n);
	m[2][3] = -2*f*n/(f-n);
	m[3][2] = -1;
}

void
orthographic(Matrix3 m, double l, double r, double b, double t, double n, double f)
{
	identity3(m);
	m[0][0] =  2/(r - l);
	m[1][1] =  2/(t - b);
	m[2][2] = -2/(f - n);
	m[0][3] = -(r + l)/(r - l);
	m[1][3] = -(t + b)/(t - b);
	m[2][3] = -(f + n)/(f - n);
}

static void
rasterize(Rastertask *task)
{
	SUparams *params;
	Primitive prim;
	FSparams fsp;
	Triangle2 t₂;
	Rectangle bbox;
	Point p;
	Point3 bc;
	Color c;
	double z, depth;

	params = task->params;
	prim = task->p;
	memmove(prim.v, task->p.v, sizeof prim.v);

	t₂.p0 = Pt2(prim.v[0].p.x, prim.v[0].p.y, 1);
	t₂.p1 = Pt2(prim.v[1].p.x, prim.v[1].p.y, 1);
	t₂.p2 = Pt2(prim.v[2].p.x, prim.v[2].p.y, 1);
	/* find the triangle's bbox and clip it against our wr */
	bbox.min.x = min(min(t₂.p0.x, t₂.p1.x), t₂.p2.x);
	bbox.min.y = min(min(t₂.p0.y, t₂.p1.y), t₂.p2.y);
	bbox.max.x = max(max(t₂.p0.x, t₂.p1.x), t₂.p2.x)+1;
	bbox.max.y = max(max(t₂.p0.y, t₂.p1.y), t₂.p2.y)+1;
	bbox.min.x = max(bbox.min.x, task->wr.min.x);
	bbox.min.y = max(bbox.min.y, task->wr.min.y);
	bbox.max.x = min(bbox.max.x, task->wr.max.x);
	bbox.max.y = min(bbox.max.y, task->wr.max.y);
	fsp.su = params;
	memset(&fsp.v, 0, sizeof fsp.v);

	for(p.y = bbox.min.y; p.y < bbox.max.y; p.y++)
		for(p.x = bbox.min.x; p.x < bbox.max.x; p.x++){
			bc = barycoords(t₂, Pt2(p.x,p.y,1));
			if(bc.x < 0 || bc.y < 0 || bc.z < 0)
				continue;

			z = fberp(prim.v[0].p.z, prim.v[1].p.z, prim.v[2].p.z, bc);
			depth = fclamp(z, 0, 1);
			if(depth <= params->fb->zb[p.x + p.y*Dx(params->fb->r)])
				continue;
			params->fb->zb[p.x + p.y*Dx(params->fb->r)] = depth;

			/* interpolate z⁻¹ and get actual z */
			z = fberp(prim.v[0].p.w, prim.v[1].p.w, prim.v[2].p.w, bc);
			z = 1.0/(z < 1e-5? 1e-5: z);

			/* perspective-correct attribute interpolation  */
			bc.x *= prim.v[0].p.w;
			bc.y *= prim.v[1].p.w;
			bc.z *= prim.v[2].p.w;
			bc = mulpt3(bc, z);
			berpvertex(&fsp.v, &prim.v[0], &prim.v[1], &prim.v[2], bc);

			fsp.p = p;
			c = params->fshader(&fsp);
			memfillcolor(params->frag, col2ul(c));

			pixel(params->fb->cb, p, params->frag);
			delvattrs(&fsp.v);
		}
}

static void
rasterizer(void *arg)
{
	Rasterparam *rp;
	Rastertask *task;
	SUparams *params;
	Memimage *frag;
	uvlong t0;

	rp = arg;
	frag = rgb(DBlack);

	threadsetname("rasterizer %d", rp->id);

	while((task = recvp(rp->taskc)) != nil){
		t0 = nanosec();

		params = task->params;
		/* end of job */
		if(params->entity == nil){
			if(decref(params->job) < 1){
				nbsend(params->job->donec, nil);
				free(params);
			}
			free(task);
			continue;
		}

		if(params->job->times.Rn.t0 == 0)
			params->job->times.Rn.t0 = t0;

		params->frag = frag;
		rasterize(task);

		delvattrs(&task->p.v[0]);
		delvattrs(&task->p.v[1]);
		delvattrs(&task->p.v[2]);
		params->job->times.Rn.t1 = nanosec();
		free(params);
		free(task);
	}
}

static void
tilerdurden(void *arg)
{
	Tilerparam *tp;
	SUparams *params, *newparams;
	Rastertask *task;
	VSparams vsp;
	OBJVertex *verts, *tverts, *nverts;	/* geometric, texture and normals vertices */
	OBJIndexArray *idxtab;
	OBJElem **ep;
	Point3 n;				/* surface normal */
	Primitive *p;				/* primitives to raster */
	Rectangle *wr, bbox;
	Channel **taskchans;
	ulong Δy, nproc;
	int i, np;
	uvlong t0;

	tp = arg;
	p = emalloc(sizeof(*p)*16);
	taskchans = tp->taskchans;
	nproc = tp->nproc;
	wr = emalloc(nproc*sizeof(Rectangle));

	threadsetname("tilerdurden %d", tp->id);

	while((params = recvp(tp->paramsc)) != nil){
		t0 = nanosec();
		if(params->job->times.Tn.t0 == 0)
			params->job->times.Tn.t0 = t0;

		/* end of job */
		if(params->entity == nil){
			if(decref(params->job) < 1){
				params->job->ref = nproc;
				for(i = 0; i < nproc; i++){
					task = emalloc(sizeof *task);
					memset(task, 0, sizeof *task);
					task->params = params;
					sendp(taskchans[i], task);
				}
			}
			continue;
		}
		vsp.su = params;

		wr[0] = params->fb->r;
		Δy = Dy(wr[0])/nproc;
		wr[0].max.y = wr[0].min.y + Δy;
		for(i = 1; i < nproc; i++)
			wr[i] = rectaddpt(wr[i-1], Pt(0,Δy));
		if(wr[nproc-1].max.y < params->fb->r.max.y)
			wr[nproc-1].max.y = params->fb->r.max.y;

		verts = params->entity->mdl->obj->vertdata[OBJVGeometric].verts;
		tverts = params->entity->mdl->obj->vertdata[OBJVTexture].verts;
		nverts = params->entity->mdl->obj->vertdata[OBJVNormal].verts;

		for(ep = params->eb; ep != params->ee; ep++){
			np = 1;	/* start with one. after clipping it might change */

			/* TODO handle all the primitive types */

			idxtab = &(*ep)->indextab[OBJVGeometric];
			p[0].v[0].p = Pt3(verts[idxtab->indices[0]].x,
					  verts[idxtab->indices[0]].y,
					  verts[idxtab->indices[0]].z,
					  verts[idxtab->indices[0]].w);
			p[0].v[1].p = Pt3(verts[idxtab->indices[1]].x,
					  verts[idxtab->indices[1]].y,
					  verts[idxtab->indices[1]].z,
					  verts[idxtab->indices[1]].w);
			p[0].v[2].p = Pt3(verts[idxtab->indices[2]].x,
					  verts[idxtab->indices[2]].y,
					  verts[idxtab->indices[2]].z,
					  verts[idxtab->indices[2]].w);

			idxtab = &(*ep)->indextab[OBJVNormal];
			if(idxtab->nindex == 3){
				p[0].v[0].n = Vec3(nverts[idxtab->indices[0]].i,
						   nverts[idxtab->indices[0]].j,
						   nverts[idxtab->indices[0]].k);
				p[0].v[0].n = normvec3(p[0].v[0].n);
				p[0].v[1].n = Vec3(nverts[idxtab->indices[1]].i,
						   nverts[idxtab->indices[1]].j,
						   nverts[idxtab->indices[1]].k);
				p[0].v[1].n = normvec3(p[0].v[1].n);
				p[0].v[2].n = Vec3(nverts[idxtab->indices[2]].i,
						   nverts[idxtab->indices[2]].j,
						   nverts[idxtab->indices[2]].k);
				p[0].v[2].n = normvec3(p[0].v[2].n);
			}else{
				/* TODO build a list of per-vertex normals earlier */
				n = normvec3(crossvec3(subpt3(p[0].v[1].p, p[0].v[0].p), subpt3(p[0].v[2].p, p[0].v[0].p)));
				p[0].v[0].n = p[0].v[1].n = p[0].v[2].n = n;
			}

			idxtab = &(*ep)->indextab[OBJVTexture];
			if(idxtab->nindex == 3){
				p[0].v[0].uv = Pt2(tverts[idxtab->indices[0]].u,
						   tverts[idxtab->indices[0]].v, 1);
				p[0].v[1].uv = Pt2(tverts[idxtab->indices[1]].u,
						   tverts[idxtab->indices[1]].v, 1);
				p[0].v[2].uv = Pt2(tverts[idxtab->indices[2]].u,
						   tverts[idxtab->indices[2]].v, 1);
			}else{
				p[0].v[0].uv = p[0].v[1].uv = p[0].v[2].uv = Vec2(0,0);
			}

			for(i = 0; i < 3; i++){
				p[0].v[i].c = Pt3(1,1,1,1);
				p[0].v[i].mtl = (*ep)->mtl;
				p[0].v[i].attrs = nil;
				p[0].v[i].nattrs = 0;
			}

			vsp.v = &p[0].v[0];
			vsp.idx = 0;
			p[0].v[0].p = params->vshader(&vsp);
			vsp.v = &p[0].v[1];
			vsp.idx = 1;
			p[0].v[1].p = params->vshader(&vsp);
			vsp.v = &p[0].v[2];
			vsp.idx = 2;
			p[0].v[2].p = params->vshader(&vsp);

			if(!isvisible(p[0].v[0].p) || !isvisible(p[0].v[1].p) || !isvisible(p[0].v[2].p))
				np = cliptriangle(p);

			while(np--){
				p[np].v[0].p = clip2ndc(p[np].v[0].p);
				p[np].v[1].p = clip2ndc(p[np].v[1].p);
				p[np].v[2].p = clip2ndc(p[np].v[2].p);

				/* culling */
//				if(isfacingback(p[np]))
//					goto skiptri;

				p[np].v[0].p = ndc2viewport(params->fb, p[np].v[0].p);
				p[np].v[1].p = ndc2viewport(params->fb, p[np].v[1].p);
				p[np].v[2].p = ndc2viewport(params->fb, p[np].v[2].p);

				bbox.min.x = min(min(p[np].v[0].p.x, p[np].v[1].p.x), p[np].v[2].p.x);
				bbox.min.y = min(min(p[np].v[0].p.y, p[np].v[1].p.y), p[np].v[2].p.y);
				bbox.max.x = max(max(p[np].v[0].p.x, p[np].v[1].p.x), p[np].v[2].p.x)+1;
				bbox.max.y = max(max(p[np].v[0].p.y, p[np].v[1].p.y), p[np].v[2].p.y)+1;

				for(i = 0; i < nproc; i++)
					if(rectXrect(bbox,wr[i])){
						newparams = emalloc(sizeof *newparams);
						*newparams = *params;
						task = emalloc(sizeof *task);
						task->params = newparams;
						task->wr = wr[i];
						task->p.v[0] = dupvertex(&p[np].v[0]);
						task->p.v[1] = dupvertex(&p[np].v[1]);
						task->p.v[2] = dupvertex(&p[np].v[2]);
						sendp(taskchans[i], task);
					}
//skiptri:
				delvattrs(&p[np].v[0]);
				delvattrs(&p[np].v[1]);
				delvattrs(&p[np].v[2]);
			}
		}
		params->job->times.Tn.t1 = nanosec();
		free(params);
	}
}

static void
entityproc(void *arg)
{
	Channel *paramsin, **paramsout, **taskchans;
	Tilerparam *tp;
	Rasterparam *rp;
	SUparams *params, *newparams;
	OBJElem **eb, **ee;
	char *nprocs;
	ulong stride, nelems, nproc, nworkers;
	int i;
	uvlong t0;

	threadsetname("entityproc");

	paramsin = arg;
	nprocs = getenv("NPROC");
	if(nprocs == nil || (nproc = strtoul(nprocs, nil, 10)) < 2)
		nproc = 1;
	else
		nproc /= 2;
	free(nprocs);

	paramsout = emalloc(nproc*sizeof(*paramsout));
	taskchans = emalloc(nproc*sizeof(*taskchans));
	for(i = 0; i < nproc; i++){
		paramsout[i] = chancreate(sizeof(SUparams*), 8);
		tp = emalloc(sizeof *tp);
		tp->id = i;
		tp->paramsc = paramsout[i];
		tp->taskchans = taskchans;
		tp->nproc = nproc;
		proccreate(tilerdurden, tp, mainstacksize);
	}
	for(i = 0; i < nproc; i++){
		rp = emalloc(sizeof *rp);
		rp->id = i;
		rp->taskc = taskchans[i] = chancreate(sizeof(Rastertask*), 32);
		proccreate(rasterizer, rp, mainstacksize);
	}

	while((params = recvp(paramsin)) != nil){
		t0 = nanosec();
		if(params->job->times.E.t0 == 0)
			params->job->times.E.t0 = t0;

		/* end of job */
		if(params->entity == nil){
			params->job->ref = nproc;
			for(i = 0; i < nproc; i++)
				sendp(paramsout[i], params);
			continue;
		}

		eb = params->entity->mdl->elems;
		nelems = params->entity->mdl->nelems;
		ee = eb + nelems;

		if(nelems <= nproc){
			nworkers = nelems;
			stride = 1;
		}else{
			nworkers = nproc;
			stride = nelems/nproc;
		}

		for(i = 0; i < nworkers; i++){
			newparams = emalloc(sizeof *newparams);
			*newparams = *params;
			newparams->eb = eb + i*stride;
			newparams->ee = i == nworkers-1? ee: newparams->eb + stride;
			sendp(paramsout[i], newparams);
		}
		params->job->times.E.t1 = nanosec();
		free(params);
	}
}

static void
renderer(void *arg)
{
	Channel *jobc;
	Renderjob *job;
	Scene *sc;
	Entity *ent;
	SUparams *params;
	Channel *paramsc;
	uvlong time;

	threadsetname("renderer");

	jobc = arg;
	paramsc = chancreate(sizeof(SUparams*), 8);

	proccreate(entityproc, paramsc, mainstacksize);

	while((job = recvp(jobc)) != nil){
		time = nanosec();
		job->times.R.t0 = time;
		sc = job->scene;
		if(sc->nents < 1){
			nbsend(job->donec, nil);
			continue;
		}

		for(ent = sc->ents.next; ent != &sc->ents; ent = ent->next){
			params = emalloc(sizeof *params);
			memset(params, 0, sizeof *params);
			params->fb = job->fb;
			params->job = job;
			params->entity = ent;
			params->uni_time = time;
			params->vshader = job->shaders->vshader;
			params->fshader = job->shaders->fshader;
			sendp(paramsc, params);
		}
		/* mark end of job */
		params = emalloc(sizeof *params);
		memset(params, 0, sizeof *params);
		params->job = job;
		sendp(paramsc, params);

		job->times.R.t1 = nanosec();
	}
}

Renderer *
initgraphics(void)
{
	Renderer *r;

	r = emalloc(sizeof *r);
	r->c = chancreate(sizeof(Renderjob*), 8);
	proccreate(renderer, r->c, mainstacksize);
	return r;
}
