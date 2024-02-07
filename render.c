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


static void
pixel(Memimage *dst, Point p, Memimage *src)
{
	if(dst == nil || src == nil)
		return;

	memimagedraw(dst, rectaddpt(UR, p), src, ZP, nil, ZP, SoverD);
}

/*
 * it only processes quads for now.
 */
static int
triangulate(OBJElem **newe, OBJElem *e)
{
	OBJIndexArray *newidxtab;
	OBJIndexArray *idxtab;

	idxtab = &e->indextab[OBJVGeometric];
	newe[0] = emalloc(sizeof *newe[0]);
	newe[0]->type = OBJEFace;
	newidxtab = &newe[0]->indextab[OBJVGeometric];
	newidxtab->nindex = 3;
	newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
	newidxtab->indices[0] = idxtab->indices[0];
	newidxtab->indices[1] = idxtab->indices[1];
	newidxtab->indices[2] = idxtab->indices[2];
	idxtab = &e->indextab[OBJVTexture];
	if(idxtab->nindex > 0){
		newidxtab = &newe[0]->indextab[OBJVTexture];
		newidxtab->nindex = 3;
		newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
		newidxtab->indices[0] = idxtab->indices[0];
		newidxtab->indices[1] = idxtab->indices[1];
		newidxtab->indices[2] = idxtab->indices[2];
	}
	idxtab = &e->indextab[OBJVNormal];
	if(idxtab->nindex > 0){
		newidxtab = &newe[0]->indextab[OBJVNormal];
		newidxtab->nindex = 3;
		newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
		newidxtab->indices[0] = idxtab->indices[0];
		newidxtab->indices[1] = idxtab->indices[1];
		newidxtab->indices[2] = idxtab->indices[2];
	}

	idxtab = &e->indextab[OBJVGeometric];
	newe[1] = emalloc(sizeof *newe[1]);
	newe[1]->type = OBJEFace;
	newidxtab = &newe[1]->indextab[OBJVGeometric];
	newidxtab->nindex = 3;
	newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
	newidxtab->indices[0] = idxtab->indices[0];
	newidxtab->indices[1] = idxtab->indices[2];
	newidxtab->indices[2] = idxtab->indices[3];
	idxtab = &e->indextab[OBJVTexture];
	if(idxtab->nindex > 0){
		newidxtab = &newe[1]->indextab[OBJVTexture];
		newidxtab->nindex = 3;
		newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
		newidxtab->indices[0] = idxtab->indices[0];
		newidxtab->indices[1] = idxtab->indices[2];
		newidxtab->indices[2] = idxtab->indices[3];
	}
	idxtab = &e->indextab[OBJVNormal];
	if(idxtab->nindex > 0){
		newidxtab = &newe[1]->indextab[OBJVNormal];
		newidxtab->nindex = 3;
		newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
		newidxtab->indices[0] = idxtab->indices[0];
		newidxtab->indices[1] = idxtab->indices[2];
		newidxtab->indices[2] = idxtab->indices[3];
	}

	return 2;
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

static void
mulsdm(double r[6], double m[6][4], Point3 p)
{
	int i;

	for(i = 0; i < 6; i++)
		r[i] += m[i][0]*p.x + m[i][1]*p.y + m[i][2]*p.z + m[i][3]*p.w;
}

typedef struct
{
	Vertex *v;
	ulong n;
	ulong cap;
} Polygon;

typedef struct
{
	ulong *idx;
	ulong nidx;
	ulong cap;
} Cliplist;

static int
addvert(Polygon *p, Vertex v)
{
	if(++p->n > p->cap)
		p->v = erealloc(p->v, (p->cap = p->n)*sizeof(*p->v));
	p->v[p->n-1] = v;
	return p->n;
}

static void
delvert(Polygon *p, ulong idx)
{
	if(--p->n > 1 && idx < p->cap-1)
		memmove(&p->v[idx], &p->v[idx+1], p->n);
}

static int
addidx(Cliplist *l, ulong idx)
{
	if(++l->nidx > l->cap)
		l->idx = erealloc(l->idx, (l->cap = l->nidx)*sizeof(*l->idx));
	l->idx[l->nidx-1] = idx;
	return l->nidx;
}

static int
idxcmp(ulong *a, ulong *b)
{
	return *a - *b;
}

static void
reapverts(Polygon *p, Cliplist *l)
{
	qsort(l->idx, l->nidx, sizeof(l->idx[0]), (int(*)(void*,void*))idxcmp);
	while(l->nidx--)
		delvert(p, l->idx[l->nidx]);
}

static int
cliptriangle(Triangle *t)
{
	/* TODO implement homogeneous clipping procedure */
	/*
	 * requirements:
	 *
	 * - normal and color attributes are carried over to the new
	 *   intersecting points without modification.
	 * - uv coordinates must be adjusted in proportion to the new
	 *   points.
	 */
	enum { L, R, B, T, F, N };
	/* signed distance from each clipping plane */
	static double sdm[6][4] = {
		 1,  0,  0, 1,
		-1,  0,  0, 1,
		 0,  1,  0, 1,
		 0, -1,  0, 1,
		 0,  0,  1, 1,
		 0,  0, -1, 1,
	}, sd0[6], sd1[6];
	Polygon V;		/* new polygon verts */
	Cliplist D;		/* verts to delete */
	Vertex v;		/* new vertex (line-plane intersection) */
	int i, j;

	if(!isvisible(t[0][0].p) && !isvisible(t[0][1].p) && !isvisible(t[0][2].p))
		return 0;

	memset(&V, 0, sizeof V);
	memset(&D, 0, sizeof D);
	/* initialize with the original triangle */
//	for(i = 0; i < 3; i++)
//		addvert(&V, t[0][i]);
//
//	for(i = 0; i < V.n-1; i++){
//		memset(sd0, 0, sizeof sd0);
//		memset(sd1, 0, sizeof sd1);
//		mulsdm(sd0, sdm, V.v[i].p);
//		mulsdm(sd1, sdm, V.v[i+1].p);
//
//		for(j = 0; j < 6; j++){
//			if(sd0[i] < 0 && sd1[i] < 0){
//				addidx(&D, i);
//				addidx(&D, i+1);
//			}else if(sd0[i] < 0){
//				addidx(&D, i);
//			}else if(sd1[i] < 0){
//				addidx(&D, i+1);
//			}
//			reapverts(&V, &D);
//		}
//	}

	return 1;
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
	p.w = 1.0/p.w;
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
rasterize(SUparams *params, Triangle t, Memimage *frag)
{
	FSparams fsp;
	Triangle2 t₂, tt₂;
	Rectangle bbox;
	Point p, tp;
	Point3 bc;
	double z, depth;
	uchar cbuf[4];

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
	cbuf[0] = 0xFF;
	fsp.su = params;
	fsp.frag = frag;
	fsp.cbuf = cbuf;

	/* perspective-divide the attributes */
//	t[0].c = mulpt3(t[0].c, t[0].p.w);
//	t[1].c = mulpt3(t[1].c, t[1].p.w);
//	t[2].c = mulpt3(t[2].c, t[2].p.w);
	t[0].uv = mulpt2(t[0].uv, t[0].p.w);
	t[1].uv = mulpt2(t[1].uv, t[1].p.w);
	t[2].uv = mulpt2(t[2].uv, t[2].p.w);

	for(p.y = bbox.min.y; p.y < bbox.max.y; p.y++)
		for(p.x = bbox.min.x; p.x < bbox.max.x; p.x++){
			bc = barycoords(t₂, Pt2(p.x,p.y,1));
			if(bc.x < 0 || bc.y < 0 || bc.z < 0)
				continue;

			z = t[0].p.z*bc.x + t[1].p.z*bc.y + t[2].p.z*bc.z;
			depth = fclamp(z, 0, 1);
			lock(&params->fb->zbuflk);
			if(depth <= params->fb->zbuf[p.x + p.y*Dx(params->fb->r)]){
				unlock(&params->fb->zbuflk);
				continue;
			}
			params->fb->zbuf[p.x + p.y*Dx(params->fb->r)] = depth;
			unlock(&params->fb->zbuflk);

			/* lerp z⁻¹ and get actual z */
			z = t[0].p.w*bc.x + t[1].p.w*bc.y + t[2].p.w*bc.z;
			z = 1.0/(z < 1e-6? 1e-6: z);

			/* lerp attribute and dissolve perspective */
//			t[0].c = mulpt3(t[0].c, bc.x*z);
//			t[1].c = mulpt3(t[1].c, bc.y*z);
//			t[2].c = mulpt3(t[2].c, bc.z*z);

			cbuf[0] = 0xFF;
			if((t[0].uv.w + t[1].uv.w + t[2].uv.w) != 0){
				tt₂.p0 = mulpt2(t[0].uv, bc.x*z);
				tt₂.p1 = mulpt2(t[1].uv, bc.y*z);
				tt₂.p2 = mulpt2(t[2].uv, bc.z*z);

				tp.x = (tt₂.p0.x + tt₂.p1.x + tt₂.p2.x)*Dx(params->modeltex->r);
				tp.y = (1 - (tt₂.p0.y + tt₂.p1.y + tt₂.p2.y))*Dy(params->modeltex->r);

				switch(params->modeltex->chan){
				case RGB24:
					unloadmemimage(params->modeltex, rectaddpt(UR, tp), cbuf+1, sizeof cbuf - 1);
					break;
				case RGBA32:
					unloadmemimage(params->modeltex, rectaddpt(UR, tp), cbuf, sizeof cbuf);
					break;
				}
			}else
				memset(cbuf+1, 0xFF, sizeof cbuf - 1);

			fsp.p = p;
			fsp.bc = bc;
			pixel(params->fb->cb, p, params->fshader(&fsp));
		}
}

static void
shaderunit(void *arg)
{
	SUparams *params;
	VSparams vsp;
	Memimage *frag;
	OBJVertex *verts, *tverts, *nverts;	/* geometric, texture and normals vertices */
	OBJIndexArray *idxtab;
	OBJElem **ep;
	Point3 n;				/* surface normal */
	Triangle t[7-2];			/* triangles to raster */
	int nt;

	params = arg;
	vsp.su = params;
	frag = rgb(DBlack);

	threadsetname("shader unit #%d", params->id);

	verts = params->model->vertdata[OBJVGeometric].verts;
	tverts = params->model->vertdata[OBJVTexture].verts;
	nverts = params->model->vertdata[OBJVNormal].verts;

	for(ep = params->b; ep != params->e; ep++){
		nt = 1;	/* start with one. after clipping it might change */

		idxtab = &(*ep)->indextab[OBJVGeometric];
		t[0][0].p = Pt3(verts[idxtab->indices[0]].x,verts[idxtab->indices[0]].y,verts[idxtab->indices[0]].z,verts[idxtab->indices[0]].w);
		t[0][1].p = Pt3(verts[idxtab->indices[1]].x,verts[idxtab->indices[1]].y,verts[idxtab->indices[1]].z,verts[idxtab->indices[1]].w);
		t[0][2].p = Pt3(verts[idxtab->indices[2]].x,verts[idxtab->indices[2]].y,verts[idxtab->indices[2]].z,verts[idxtab->indices[2]].w);

		idxtab = &(*ep)->indextab[OBJVNormal];
		if(idxtab->nindex == 3){
			t[0][0].n = Vec3(nverts[idxtab->indices[0]].i, nverts[idxtab->indices[0]].j, nverts[idxtab->indices[0]].k);
			t[0][0].n = normvec3(t[0][0].n);
			t[0][1].n = Vec3(nverts[idxtab->indices[1]].i, nverts[idxtab->indices[1]].j, nverts[idxtab->indices[1]].k);
			t[0][1].n = normvec3(t[0][1].n);
			t[0][2].n = Vec3(nverts[idxtab->indices[2]].i, nverts[idxtab->indices[2]].j, nverts[idxtab->indices[2]].k);
			t[0][2].n = normvec3(t[0][2].n);
		}else{
			/* TODO build a list of per-vertex normals earlier */
			n = normvec3(crossvec3(subpt3(t[0][2].p, t[0][0].p), subpt3(t[0][1].p, t[0][0].p)));
			t[0][0].n = t[0][1].n = t[0][2].n = mulpt3(n, -1);
		}

		idxtab = &(*ep)->indextab[OBJVTexture];
		if(params->modeltex != nil && idxtab->nindex == 3){
			t[0][0].uv = Pt2(tverts[idxtab->indices[0]].u, tverts[idxtab->indices[0]].v, 1);
			t[0][1].uv = Pt2(tverts[idxtab->indices[1]].u, tverts[idxtab->indices[1]].v, 1);
			t[0][2].uv = Pt2(tverts[idxtab->indices[2]].u, tverts[idxtab->indices[2]].v, 1);
		}else{
			t[0][0].uv = t[0][1].uv = t[0][2].uv = Vec2(0,0);
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
			t[nt][0].p = ndc2viewport(params->fb, clip2ndc(t[nt][0].p));
			t[nt][1].p = ndc2viewport(params->fb, clip2ndc(t[nt][1].p));
			t[nt][2].p = ndc2viewport(params->fb, clip2ndc(t[nt][2].p));

			rasterize(params, t[nt], frag);
		}
	}

	freememimage(frag);
	sendp(params->donec, nil);
	free(params);
	threadexits(nil);
}

void
shade(Framebuf *fb, OBJ *model, Memimage *modeltex, Shader *s, ulong nprocs)
{
	static int nparts, nworkers;
	static OBJElem **elems = nil;
	OBJElem *trielems[2];
	int i, nelems;
	uvlong time;
	OBJObject *o;
	OBJElem *e;
	OBJIndexArray *idxtab;
	SUparams *params;
	Channel *donec;

	if(elems == nil){
		nelems = 0;
		for(i = 0; i < nelem(model->objtab); i++)
			for(o = model->objtab[i]; o != nil; o = o->next)
				for(e = o->child; e != nil; e = e->next){
					idxtab = &e->indextab[OBJVGeometric];
					/* discard non-triangles */
					if(e->type != OBJEFace || (idxtab->nindex != 3 && idxtab->nindex != 4))
						continue;
					if(idxtab->nindex == 4){
						triangulate(trielems, e);
						nelems += 2;
						elems = erealloc(elems, nelems*sizeof(*elems));
						elems[nelems-2] = trielems[0];
						elems[nelems-1] = trielems[1];
					}else{
						elems = erealloc(elems, ++nelems*sizeof(*elems));
						elems[nelems-1] = e;
					}
				}
		if(nelems < nprocs){
			nworkers = nelems;
			nparts = 1;
		}else{
			nworkers = nprocs;
			nparts = nelems/nprocs;
		}
	}
	time = nanosec();

	donec = chancreate(sizeof(void*), 0);

	for(i = 0; i < nworkers; i++){
		params = emalloc(sizeof *params);
		params->fb = fb;
		params->b = &elems[i*nparts];
		params->e = params->b + nparts;
		params->id = i;
		params->donec = donec;
		params->model = model;
		params->modeltex = modeltex;
		params->uni_time = time;
		params->vshader = s->vshader;
		params->fshader = s->fshader;
		proccreate(shaderunit, params, mainstacksize);
//		fprint(2, "spawned su %d for elems [%d, %d)\n", params->id, i*nparts, i*nparts+nparts);
	}

	while(i--)
		recvp(donec);
	chanfree(donec);
}
