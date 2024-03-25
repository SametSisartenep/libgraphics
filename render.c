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
isfacingback(Triangle t)
{
	double sa;	/* signed area */

	sa = t[0].p.x * t[1].p.y - t[0].p.y * t[1].p.x +
	     t[1].p.x * t[2].p.y - t[1].p.y * t[2].p.x +
	     t[2].p.x * t[0].p.y - t[2].p.y * t[0].p.x;
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
cliptriangle(Triangle *t)
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
		addvert(&Vin, t[0][i]);

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
			t[nt][0] = i < Vout.n-2-1? dupvertex(&Vout.v[0]): Vout.v[0];
			t[nt][1] = Vout.v[i+1];
			t[nt][2] = i < Vout.n-2-1? dupvertex(&Vout.v[i+2]): Vout.v[i+2];
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
rasterize(SUparams *params, Triangle t)
{
	FSparams fsp;
	Triangle2 t₂;
	Rectangle bbox;
	Point p;
	Point3 bc;
	Color c;
	double z, depth;

	t₂.p0 = Pt2(t[0].p.x, t[0].p.y, 1);
	t₂.p1 = Pt2(t[1].p.x, t[1].p.y, 1);
	t₂.p2 = Pt2(t[2].p.x, t[2].p.y, 1);
	/* find the triangle's bbox and clip it against the fb */
	bbox = Rect(
		min(min(t₂.p0.x, t₂.p1.x), t₂.p2.x), min(min(t₂.p0.y, t₂.p1.y), t₂.p2.y),
		max(max(t₂.p0.x, t₂.p1.x), t₂.p2.x)+1, max(max(t₂.p0.y, t₂.p1.y), t₂.p2.y)+1
	);
	bbox.min.x = max(bbox.min.x, params->fb->r.min.x);
	bbox.min.y = max(bbox.min.y, params->fb->r.min.y);
	bbox.max.x = min(bbox.max.x, params->fb->r.max.x);
	bbox.max.y = min(bbox.max.y, params->fb->r.max.y);
	fsp.su = params;
	memset(&fsp.v, 0, sizeof fsp.v);

	for(p.y = bbox.min.y; p.y < bbox.max.y; p.y++)
		for(p.x = bbox.min.x; p.x < bbox.max.x; p.x++){
			bc = barycoords(t₂, Pt2(p.x,p.y,1));
			if(bc.x < 0 || bc.y < 0 || bc.z < 0)
				continue;

			z = fberp(t[0].p.z, t[1].p.z, t[2].p.z, bc);
			depth = fclamp(z, 0, 1);
			lock(&params->fb->zbuflk);
			if(depth <= params->fb->zbuf[p.x + p.y*Dx(params->fb->r)]){
				unlock(&params->fb->zbuflk);
				continue;
			}
			params->fb->zbuf[p.x + p.y*Dx(params->fb->r)] = depth;
			unlock(&params->fb->zbuflk);

			/* interpolate z⁻¹ and get actual z */
			z = fberp(t[0].p.w, t[1].p.w, t[2].p.w, bc);
			z = 1.0/(z < 1e-5? 1e-5: z);

			/* perspective-correct attribute interpolation  */
			bc.x *= t[0].p.w;
			bc.y *= t[1].p.w;
			bc.z *= t[2].p.w;
			bc = mulpt3(bc, z);
			berpvertex(&fsp.v, &t[0], &t[1], &t[2], bc);

			fsp.p = p;
			c = params->fshader(&fsp);
			memfillcolor(params->frag, col2ul(c));

			pixel(params->fb->cb, p, params->frag);
			delvattrs(&fsp.v);
		}
}

static void
entityproc(void *arg)
{
	Channel *paramsc;
	SUparams *params;
	VSparams vsp;
	OBJVertex *verts, *tverts, *nverts;	/* geometric, texture and normals vertices */
	OBJIndexArray *idxtab;
	OBJElem **ep, **eb, **ee;
	Point3 n;				/* surface normal */
	Triangle *t;				/* triangles to raster */
	int i, nt;

	threadsetname("entityproc");

	paramsc = arg;
	t = emalloc(sizeof(*t)*16);

	while((params = recvp(paramsc)) != nil){
		vsp.su = params;

		verts = params->entity->mdl->obj->vertdata[OBJVGeometric].verts;
		tverts = params->entity->mdl->obj->vertdata[OBJVTexture].verts;
		nverts = params->entity->mdl->obj->vertdata[OBJVNormal].verts;

		eb = params->entity->mdl->elems;
		ee = eb + params->entity->mdl->nelems;

		for(ep = eb; ep != ee; ep++){
			nt = 1;	/* start with one. after clipping it might change */

			idxtab = &(*ep)->indextab[OBJVGeometric];
			t[0][0].p = Pt3(verts[idxtab->indices[0]].x,
					verts[idxtab->indices[0]].y,
					verts[idxtab->indices[0]].z,
					verts[idxtab->indices[0]].w);
			t[0][1].p = Pt3(verts[idxtab->indices[1]].x,
					verts[idxtab->indices[1]].y,
					verts[idxtab->indices[1]].z,
					verts[idxtab->indices[1]].w);
			t[0][2].p = Pt3(verts[idxtab->indices[2]].x,
					verts[idxtab->indices[2]].y,
					verts[idxtab->indices[2]].z,
					verts[idxtab->indices[2]].w);

			idxtab = &(*ep)->indextab[OBJVNormal];
			if(idxtab->nindex == 3){
				t[0][0].n = Vec3(nverts[idxtab->indices[0]].i,
						 nverts[idxtab->indices[0]].j,
						 nverts[idxtab->indices[0]].k);
				t[0][0].n = normvec3(t[0][0].n);
				t[0][1].n = Vec3(nverts[idxtab->indices[1]].i,
						 nverts[idxtab->indices[1]].j,
						 nverts[idxtab->indices[1]].k);
				t[0][1].n = normvec3(t[0][1].n);
				t[0][2].n = Vec3(nverts[idxtab->indices[2]].i,
						 nverts[idxtab->indices[2]].j,
						 nverts[idxtab->indices[2]].k);
				t[0][2].n = normvec3(t[0][2].n);
			}else{
				/* TODO build a list of per-vertex normals earlier */
				n = normvec3(crossvec3(subpt3(t[0][1].p, t[0][0].p), subpt3(t[0][2].p, t[0][0].p)));
				t[0][0].n = t[0][1].n = t[0][2].n = n;
			}

			idxtab = &(*ep)->indextab[OBJVTexture];
			if(idxtab->nindex == 3){
				t[0][0].uv = Pt2(tverts[idxtab->indices[0]].u,
						 tverts[idxtab->indices[0]].v, 1);
				t[0][1].uv = Pt2(tverts[idxtab->indices[1]].u,
						 tverts[idxtab->indices[1]].v, 1);
				t[0][2].uv = Pt2(tverts[idxtab->indices[2]].u,
						 tverts[idxtab->indices[2]].v, 1);
			}else{
				t[0][0].uv = t[0][1].uv = t[0][2].uv = Vec2(0,0);
			}

			for(i = 0; i < 3; i++){
				t[0][i].c = Pt3(1,1,1,1);
				t[0][i].mtl = (*ep)->mtl;
				t[0][i].attrs = nil;
				t[0][i].nattrs = 0;
			}

			vsp.v = &t[0][0];
			vsp.idx = 0;
			t[0][0].p = params->vshader(&vsp);
			vsp.v = &t[0][1];
			vsp.idx = 1;
			t[0][1].p = params->vshader(&vsp);
			vsp.v = &t[0][2];
			vsp.idx = 2;
			t[0][2].p = params->vshader(&vsp);

			if(!isvisible(t[0][0].p) || !isvisible(t[0][1].p) || !isvisible(t[0][2].p))
				nt = cliptriangle(t);

			while(nt--){
				t[nt][0].p = clip2ndc(t[nt][0].p);
				t[nt][1].p = clip2ndc(t[nt][1].p);
				t[nt][2].p = clip2ndc(t[nt][2].p);

				/* culling */
//				if(isfacingback(t[nt]))
//					goto skiptri;

				t[nt][0].p = ndc2viewport(params->fb, t[nt][0].p);
				t[nt][1].p = ndc2viewport(params->fb, t[nt][1].p);
				t[nt][2].p = ndc2viewport(params->fb, t[nt][2].p);

				rasterize(params, t[nt]);
//skiptri:
				delvattrs(&t[nt][0]);
				delvattrs(&t[nt][1]);
				delvattrs(&t[nt][2]);
			}
		}
		sendp(params->donec, params);
	}
}

static void
renderer(void *arg)
{
	Channel *jobc;
	Jobqueue jobq;
	Renderjob *job;
	Scene *sc;
	Entity *ent;
	SUparams *params, *params2;
	Channel *paramsc, *donec;

	threadsetname("renderer");

	jobc = arg;
	jobq.tl = jobq.hd = nil;
	ent = nil;
	paramsc = chancreate(sizeof(SUparams*), 8);
	donec = chancreate(sizeof(SUparams*), 0);

	proccreate(entityproc, paramsc, mainstacksize);

	enum { JOB, PARM, DONE };
	Alt a[] = {
	 [JOB]	{jobc, &job, CHANRCV},
	 [PARM]	{paramsc, &params, CHANNOP},
	 [DONE]	{donec, &params2, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;)
		switch(alt(a)){
		case JOB:
			sc = job->scene;
			job->nrem = sc->nents;
			job->lastid = 0;
			job->time0 = nanosec();

			if(jobq.tl == nil){
				jobq.tl = jobq.hd = job;
				ent = sc->ents.next;
				a[PARM].op = CHANSND;
				goto sendparams;
			}else
				jobq.tl = jobq.tl->next = job;
			break;
		case PARM:
sendparams:
			job = jobq.hd;
			sc = job->scene;

			if(ent != nil && ent != &sc->ents){
				params = emalloc(sizeof *params);
				memset(params, 0, sizeof *params);
				params->fb = job->fb;
				params->id = job->lastid++;
				params->frag = rgb(DBlack);
				params->donec = donec;
				params->job = job;
				params->entity = ent;
				params->uni_time = job->time0;
				params->vshader = job->shaders->vshader;
				params->fshader = job->shaders->fshader;
				ent = ent->next;
			}else{
				jobq.hd = job->next;
				if((job = jobq.hd) != nil){
					ent = job->scene->ents.next;
					goto sendparams;
				}

				jobq.tl = jobq.hd;
				a[PARM].op = CHANNOP;
			}
			break;
		case DONE:
			if(--params2->job->nrem < 1)
				send(params2->job->donec, nil);

			freememimage(params2->frag);
			free(params2);
			break;
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
