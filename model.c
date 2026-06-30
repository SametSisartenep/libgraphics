#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

Vertex
mkvert(void)
{
	return (Vertex){NaI, NaI, NaI, NaI};
}

Primitive
mkprim(int type)
{
	Primitive prim;

	prim.type = type;
	prim.v[0] = prim.v[1] = prim.v[2] = NaI;
	prim.tangent = NaI;
	prim.mtl = NaI;
	return prim;
}

Material *
newmaterial(char *name)
{
	Material *mtl;

	if(name == nil || name[0] == '\0'){
		werrstr("needs a name");
		return nil;
	}

	mtl = _emalloc(sizeof *mtl);
	memset(mtl, 0, sizeof *mtl);
	mtl->name = _estrdup(name);
	mtl->ambient = Pt3(1,1,1,1);
	mtl->diffuse = Pt3(1,1,1,1);
	mtl->specular = Pt3(1,1,1,1);
	return mtl;
}

void
delmaterial(Material *mtl)
{
	freetexture(mtl->diffusemap);
	freetexture(mtl->specularmap);
	freetexture(mtl->normalmap);
	free(mtl->name);
	free(mtl);
}

static ulong
model_addposition(Model *m, Point3 p)
{
	return bunchadd(m->positions, &p);
}

static ulong
model_addnormal(Model *m, Point3 n)
{
	return bunchadd(m->normals, &n);
}

static ulong
model_addtexcoord(Model *m, Point2 t)
{
	return bunchadd(m->texcoords, &t);
}

static ulong
model_addcolor(Model *m, Color c)
{
	return bunchadd(m->colors, &c);
}

static ulong
model_addtangent(Model *m, Point3 T)
{
	return bunchadd(m->tangents, &T);
}

static ulong
model_addvert(Model *m, Vertex v)
{
	return bunchadd(m->verts, &v);
}

static ulong
model_addprim(Model *m, Primitive P)
{
	return bunchadd(m->prims, &P);
}

static ulong
model_addmaterial(Model *m, Material mtl)
{
	assert(mtl.name != nil);
	return bunchadd(m->materials, &mtl);
}

static ulong
model_findmaterial(Model *m, char *name)
{
	Material *mtl, *mtls, *mtle;

	if(name == nil)
		return NaI;

	mtl = mtls = m->materials->items;
	mtle = mtls + m->materials->nitems;
	while(mtl < mtle){
		if(strcmp(mtl->name, name) == 0)
			return mtl - mtls;
		mtl++;
	}
	return NaI;
}

Model *
newmodel(void)
{
	Model *m;

	m = _emalloc(sizeof *m);
	memset(m, 0, sizeof *m);
	m->positions	= allocbunch(sizeof(Point3));
	m->normals	= allocbunch(sizeof(Point3));
	m->texcoords	= allocbunch(sizeof(Point2));
	m->colors	= allocbunch(sizeof(Color));
	m->tangents	= allocbunch(sizeof(Point3));
	m->verts	= allocbunch(sizeof(Vertex));
	m->prims	= allocbunch(sizeof(Primitive));
	m->materials	= allocbunch(sizeof(Material));
	m->addposition	= model_addposition;
	m->addnormal	= model_addnormal;
	m->addtexcoord	= model_addtexcoord;
	m->addcolor	= model_addcolor;
	m->addtangent	= model_addtangent;
	m->addvert	= model_addvert;
	m->addprim	= model_addprim;
	m->addmaterial	= model_addmaterial;
	m->findmaterial	= model_findmaterial;
	incref(m);
	return m;
}

Model *
refmodel(Model *m)
{
	incref(m);
	return m;
}

Model *
dupmodel(Model *m)
{
	Model *n;

	n = newmodel();
	n->name		= m->name? _estrdup(m->name): nil;
	n->positions	= refbunch(m->positions);
	n->normals	= refbunch(m->normals);
	n->texcoords	= refbunch(m->texcoords);
	n->colors	= refbunch(m->colors);
	n->tangents	= refbunch(m->tangents);
	n->verts	= refbunch(m->verts);
	n->prims	= refbunch(m->prims);
	n->materials	= refbunch(m->materials);
	return n;
}

void
delmodel(Model *m)
{
	if(m == nil)
		return;

	if(decref(m) == 0){
		freebunch(m->positions);
		freebunch(m->normals);
		freebunch(m->texcoords);
		freebunch(m->colors);
		freebunch(m->tangents);
		freebunch(m->verts);
		freebunch(m->prims);
		freebunch(m->materials);	/* TODO this leaks material properties (name and textures). fix it */
		free(m->name);
		free(m);
	}
}

/*
 * sequential reindexing table
 *
 * the tables are processed in order for every vertex attribute.
 * if the attribute equals the old index, it's replaced by the new
 * one; if it's bigger, it's decreased by one.  otherwise it stays
 * the same.
 */
/* TODO there must be a better way to do this */
/* TODO think of a way to parallelize it */
typedef struct Reidx Reidx;
typedef struct Reidxtab Reidxtab;

struct Reidx
{
	ulong old;
	ulong new;
};

struct Reidxtab
{
	Reidx *tab;
	ulong len;
	ulong cap;
};

static void
reidxtabadd(Reidxtab *t, Reidx r)
{
	if(t->len == t->cap){
		t->cap += 8;
		t->tab = _erealloc(t->tab, t->cap * sizeof(Reidx));
	}
	t->tab[t->len++] = r;
}

static void
freereidxtab(Reidxtab *t)
{
	free(t->tab);
	memset(t, 0, sizeof(*t));
}

static void
reindexverts(Bunch *verts, Reidxtab *t, int aoff)
{
	Reidx *reidx;
	Vertex *v, *vb, *ve;
	ulong *attr;

	if(t->len == 0)
		return;

	vb = verts->items;
	ve = vb + verts->nitems;

	for(v = vb; v < ve; v++){
		attr = (ulong*)((char*)v + aoff);
		for(reidx = t->tab; reidx < t->tab+t->len; reidx++)
			if(*attr == reidx->old)
				*attr = reidx->new;
			else if(*attr > reidx->old)
				(*attr)--;
	}
}

static void
reindexprims(Bunch *prims, Reidxtab *t, int aoff)
{
	Reidx *reidx;
	Primitive *P, *Pb, *Pe;
	ulong *attr;

	if(t->len == 0)
		return;

	Pb = prims->items;
	Pe = Pb + prims->nitems;

	for(P = Pb; P < Pe; P++){
		attr = (ulong*)((char*)P + aoff);
		for(reidx = t->tab; reidx < t->tab+t->len; reidx++)
			if(*attr == reidx->old)
				*attr = reidx->new;
			else if(*attr > reidx->old)
				(*attr)--;
	}
}

static void
reindexprimverts(Bunch *prims, Reidxtab *t)
{
	Reidx *reidx;
	Primitive *P, *Pb, *Pe;
	ulong i;

	if(t->len == 0)
		return;

	Pb = prims->items;
	Pe = Pb + prims->nitems;

	for(P = Pb; P < Pe; P++)
	for(reidx = t->tab; reidx < t->tab+t->len; reidx++)
		for(i = 0; i < P->type+1; i++)
			if(P->v[i] == reidx->old)
				P->v[i] = reidx->new;
			else if(P->v[i] > reidx->old)
				P->v[i]--;
}

static void
dedup(Bunch *a, Reidxtab *t)
{
	char *p1, *p2, *pb, *pe;
	void *vp;
	ulong nitems0, i, j;

	pb = a->items;
	pe = pb + a->nitems*a->itemsize;

	if(t != nil){
		for(p1 = pb, i = 0; p1 < pe; p1 += a->itemsize, i++)
		for(p2 = p1+a->itemsize, j = i+1; p2 < pe; p2 += a->itemsize, j++)
			if(memcmp(p1, p2, a->itemsize) == 0){
				reidxtabadd(t, (Reidx){j, i});

				pe -= a->itemsize;
				if(p2 < pe){
					memmove(p2, p2+a->itemsize, pe - p2);
					p2 -= a->itemsize;
					j--;
				}
			}
	}else{
		for(p1 = pb; p1 < pe; p1 += a->itemsize)
		for(p2 = p1+a->itemsize; p2 < pe; p2 += a->itemsize)
			if(memcmp(p1, p2, a->itemsize) == 0){
				pe -= a->itemsize;
				if(p2 < pe){
					memmove(p2, p2+a->itemsize, pe - p2);
					p2 -= a->itemsize;
				}
			}
	}

	nitems0 = a->nitems;
	a->nitems = (pe - pb)/a->itemsize;
	if(a->nitems != nitems0){
		/* try to shrink it */
		vp = realloc(a->items, a->nitems * a->itemsize);
		if(vp != nil)
			a->items = vp;
	}
}

void
compactmodel(Model *m)
{
	Reidxtab itab;

	memset(&itab, 0, sizeof(itab));

	dedup(m->positions, &itab);
	reindexverts(m->verts, &itab, offsetof(Vertex, p));
	itab.len = 0;

	dedup(m->normals, &itab);
	reindexverts(m->verts, &itab, offsetof(Vertex, n));
	itab.len = 0;

	dedup(m->texcoords, &itab);
	reindexverts(m->verts, &itab, offsetof(Vertex, uv));
	itab.len = 0;

	dedup(m->colors, &itab);
	reindexverts(m->verts, &itab, offsetof(Vertex, c));
	itab.len = 0;

	dedup(m->tangents, &itab);
	reindexprims(m->prims, &itab, offsetof(Primitive, tangent));
	itab.len = 0;

	dedup(m->materials, &itab);
	reindexprims(m->prims, &itab, offsetof(Primitive, mtl));
	itab.len = 0;

	dedup(m->verts, &itab);
	reindexprimverts(m->prims, &itab);

	dedup(m->prims, nil);

	freereidxtab(&itab);
}
