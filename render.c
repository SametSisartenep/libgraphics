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
	newidxtab = &newe[0]->indextab[OBJVTexture];
	newidxtab->nindex = 3;
	newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
	newidxtab->indices[0] = idxtab->indices[0];
	newidxtab->indices[1] = idxtab->indices[1];
	newidxtab->indices[2] = idxtab->indices[2];
	idxtab = &e->indextab[OBJVNormal];
	newidxtab = &newe[0]->indextab[OBJVNormal];
	newidxtab->nindex = 3;
	newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
	newidxtab->indices[0] = idxtab->indices[0];
	newidxtab->indices[1] = idxtab->indices[1];
	newidxtab->indices[2] = idxtab->indices[2];

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
	newidxtab = &newe[1]->indextab[OBJVTexture];
	newidxtab->nindex = 3;
	newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
	newidxtab->indices[0] = idxtab->indices[0];
	newidxtab->indices[1] = idxtab->indices[2];
	newidxtab->indices[2] = idxtab->indices[3];
	idxtab = &e->indextab[OBJVNormal];
	newidxtab = &newe[1]->indextab[OBJVNormal];
	newidxtab->nindex = 3;
	newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
	newidxtab->indices[0] = idxtab->indices[0];
	newidxtab->indices[1] = idxtab->indices[2];
	newidxtab->indices[2] = idxtab->indices[3];

	return 2;
}

Point3
world2vcs(Camera *c, Point3 p)
{
	return rframexform3(p, *c);
}

Point3
vcs2ndc(Camera *c, Point3 p)
{
	return xform3(p, c->proj);
}

Point3
world2ndc(Camera *c, Point3 p)
{
	return vcs2ndc(c, world2vcs(c, p));
}

Point3
ndc2viewport(Camera *c, Point3 p)
{
	Matrix3 view;

	identity3(view);
	view[0][3] = c->vp->fbctl->fb[0]->r.max.x/2.0;
	view[1][3] = c->vp->fbctl->fb[0]->r.max.y/2.0;
	view[2][3] = 1.0/2.0;
	view[0][0] = Dx(c->vp->fbctl->fb[0]->r)/2.0;
	view[1][1] = -Dy(c->vp->fbctl->fb[0]->r)/2.0;
	view[2][2] = 1.0/2.0;

	return xform3(p, view);
}

void
perspective(Matrix3 m, double fov, double a, double n, double f)
{
	double cotan;

	cotan = 1/tan(fov/2);
	identity3(m);
	m[0][0] =  cotan/a;
	m[1][1] =  cotan;
	m[2][2] = (f+n)/(f-n);
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
	double z, w, depth;
	uchar cbuf[4];

	t₂.p0 = Pt2(t[0].p.x/t[0].p.w, t[0].p.y/t[0].p.w, 1);
	t₂.p1 = Pt2(t[1].p.x/t[1].p.w, t[1].p.y/t[1].p.w, 1);
	t₂.p2 = Pt2(t[2].p.x/t[2].p.w, t[2].p.y/t[2].p.w, 1);
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

	for(p.y = bbox.min.y; p.y < bbox.max.y; p.y++)
		for(p.x = bbox.min.x; p.x < bbox.max.x; p.x++){
			bc = barycoords(t₂, Pt2(p.x,p.y,1));
			if(bc.x < 0 || bc.y < 0 || bc.z < 0)
				continue;

			z = t[0].p.z*bc.x + t[1].p.z*bc.y + t[2].p.z*bc.z;
			w = t[0].p.w*bc.x + t[1].p.w*bc.y + t[2].p.w*bc.z;
			depth = fclamp(z/w, 0, 1);
			lock(&params->fb->zbuflk);
			if(depth <= params->fb->zbuf[p.x + p.y*Dx(params->fb->r)]){
				unlock(&params->fb->zbuflk);
				continue;
			}
			params->fb->zbuf[p.x + p.y*Dx(params->fb->r)] = depth;
			unlock(&params->fb->zbuflk);

			cbuf[0] = 0xFF;
			if((t[0].uv.w + t[1].uv.w + t[2].uv.w) != 0){
				tt₂.p0 = mulpt2(t[0].uv, bc.x);
				tt₂.p1 = mulpt2(t[1].uv, bc.y);
				tt₂.p2 = mulpt2(t[2].uv, bc.z);

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
	Triangle t;
	Point3 n;				/* surface normal */

	params = arg;
	vsp.su = params;
	frag = rgb(DBlack);

	threadsetname("shader unit #%d", params->id);

	verts = params->model->vertdata[OBJVGeometric].verts;
	tverts = params->model->vertdata[OBJVTexture].verts;
	nverts = params->model->vertdata[OBJVNormal].verts;

	for(ep = params->b; ep != params->e; ep++){
		idxtab = &(*ep)->indextab[OBJVGeometric];

		t[0].p = Pt3(verts[idxtab->indices[0]].x,verts[idxtab->indices[0]].y,verts[idxtab->indices[0]].z,verts[idxtab->indices[0]].w);
		t[1].p = Pt3(verts[idxtab->indices[1]].x,verts[idxtab->indices[1]].y,verts[idxtab->indices[1]].z,verts[idxtab->indices[1]].w);
		t[2].p = Pt3(verts[idxtab->indices[2]].x,verts[idxtab->indices[2]].y,verts[idxtab->indices[2]].z,verts[idxtab->indices[2]].w);

		idxtab = &(*ep)->indextab[OBJVNormal];
		if(idxtab->nindex == 3){
			t[0].n = Vec3(nverts[idxtab->indices[0]].i, nverts[idxtab->indices[0]].j, nverts[idxtab->indices[0]].k);
			t[0].n = normvec3(t[0].n);
			t[1].n = Vec3(nverts[idxtab->indices[1]].i, nverts[idxtab->indices[1]].j, nverts[idxtab->indices[1]].k);
			t[1].n = normvec3(t[1].n);
			t[2].n = Vec3(nverts[idxtab->indices[2]].i, nverts[idxtab->indices[2]].j, nverts[idxtab->indices[2]].k);
			t[2].n = normvec3(t[2].n);
		}else{
			n = normvec3(crossvec3(subpt3(t[2].p, t[0].p), subpt3(t[1].p, t[0].p)));
			t[0].n = t[1].n = t[2].n = mulpt3(n, -1);
		}

		vsp.v = &t[0];
		vsp.idx = 0;
		t[0].p = params->vshader(&vsp);
		vsp.v = &t[1];
		vsp.idx = 1;
		t[1].p = params->vshader(&vsp);
		vsp.v = &t[2];
		vsp.idx = 2;
		t[2].p = params->vshader(&vsp);

		idxtab = &(*ep)->indextab[OBJVTexture];
		if(params->modeltex != nil && idxtab->nindex == 3){
			t[0].uv = Pt2(tverts[idxtab->indices[0]].u, tverts[idxtab->indices[0]].v, 1);
			t[1].uv = Pt2(tverts[idxtab->indices[1]].u, tverts[idxtab->indices[1]].v, 1);
			t[2].uv = Pt2(tverts[idxtab->indices[2]].u, tverts[idxtab->indices[2]].v, 1);
		}else{
			t[0].uv = t[1].uv = t[2].uv = Vec2(0,0);
		}

		rasterize(params, t, frag);
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
