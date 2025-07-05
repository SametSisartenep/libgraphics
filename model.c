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
	prim.mtl = nil;
	return prim;
}

Material *
newmaterial(char *name)
{
	Material *mtl;

	if(name == nil){
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
}

static usize
model_addposition(Model *m, Point3 p)
{
	return itemarrayadd(m->positions, &p);
}

static usize
model_addnormal(Model *m, Point3 n)
{
	return itemarrayadd(m->normals, &n);
}

static usize
model_addtexcoord(Model *m, Point2 t)
{
	return itemarrayadd(m->texcoords, &t);
}

static usize
model_addcolor(Model *m, Color c)
{
	return itemarrayadd(m->colors, &c);
}

static usize
model_addtangent(Model *m, Point3 T)
{
	return itemarrayadd(m->tangents, &T);
}

static usize
model_addvert(Model *m, Vertex v)
{
	return itemarrayadd(m->verts, &v);
}

static usize
model_addprim(Model *m, Primitive P)
{
	return itemarrayadd(m->prims, &P);
}

static int
model_addmaterial(Model *m, Material mtl)
{
	m->materials = _erealloc(m->materials, ++m->nmaterials*sizeof(*m->materials));
	m->materials[m->nmaterials-1] = mtl;
	return m->nmaterials-1;
}

static Material *
model_getmaterial(Model *m, char *name)
{
	Material *mtl;

	for(mtl = m->materials; mtl < m->materials+m->nmaterials; mtl++)
		if(strcmp(mtl->name, name) == 0)
			return mtl;
	return nil;
}

Model *
newmodel(void)
{
	Model *m;

	m = _emalloc(sizeof *m);
	memset(m, 0, sizeof *m);
	m->positions = mkitemarray(sizeof(Point3));
	m->normals = mkitemarray(sizeof(Point3));
	m->texcoords = mkitemarray(sizeof(Point2));
	m->colors = mkitemarray(sizeof(Color));
	m->tangents = mkitemarray(sizeof(Point3));
	m->verts = mkitemarray(sizeof(Vertex));
	m->prims = mkitemarray(sizeof(Primitive));
	m->addposition = model_addposition;
	m->addnormal = model_addnormal;
	m->addtexcoord = model_addtexcoord;
	m->addcolor = model_addcolor;
	m->addtangent = model_addtangent;
	m->addvert = model_addvert;
	m->addprim = model_addprim;
	m->addmaterial = model_addmaterial;
	m->getmaterial = model_getmaterial;
	return m;
}

Model *
dupmodel(Model *m)
{
	Model *nm;
	Primitive *prim, *nprim;
	int i;

	if(m == nil)
		return nil;

	nm = newmodel();
	if(m->nmaterials > 0){
		nm->nmaterials = m->nmaterials;
		nm->materials = _emalloc(nm->nmaterials*sizeof(*nm->materials));
		for(i = 0; i < m->nmaterials; i++){
			nm->materials[i] = m->materials[i];
			nm->materials[i].diffusemap = duptexture(m->materials[i].diffusemap);
			nm->materials[i].specularmap = duptexture(m->materials[i].specularmap);
			nm->materials[i].normalmap = duptexture(m->materials[i].normalmap);
			nm->materials[i].name = _estrdup(m->materials[i].name);
		}
	}
	nm->positions = dupitemarray(m->positions);
	nm->normals = dupitemarray(m->normals);
	nm->texcoords = dupitemarray(m->texcoords);
	nm->colors = dupitemarray(m->colors);
	nm->tangents = dupitemarray(m->tangents);
	nm->verts = dupitemarray(m->verts);
	nm->prims = dupitemarray(m->prims);
	for(i = 0; i < m->prims->nitems && nm->nmaterials > 0; i++){
		prim = itemarrayget(m->prims, i);
		if(prim->mtl != nil){
			nprim = itemarrayget(nm->prims, i);
			nprim->mtl = &nm->materials[prim->mtl - m->materials];
		}
	}
	return nm;
}

void
delmodel(Model *m)
{
	if(m == nil)
		return;

	while(m->nmaterials--)
		delmaterial(&m->materials[m->nmaterials]);
	free(m->materials);
	rmitemarray(m->positions);
	rmitemarray(m->normals);
	rmitemarray(m->texcoords);
	rmitemarray(m->colors);
	rmitemarray(m->tangents);
	rmitemarray(m->verts);
	rmitemarray(m->prims);
	free(m);
}

/*
 * sequential reindexing table
 *
 * the tables are processed in order (hence the sequence) for every
 * vertex attribute.  if the attribute equals the old index, it's
 * replaced by the new one; if it's bigger, it's decreased by one.
 * otherwise it stays the same.
 */
typedef struct Reidx Reidx;
typedef struct Reidxtab Reidxtab;

struct Reidx
{
	usize old;
	usize new;
};

struct Reidxtab
{
	Reidx *tab;
	usize len;
	usize cap;
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
reindexverts(ItemArray *verts, Reidxtab *t, int aoff)
{
	Reidx *reidx;
	Vertex *v, *vb, *ve;
	usize *attr;

	if(t->len == 0)
		return;

	vb = verts->items;
	ve = vb + verts->nitems;

	for(v = vb; v < ve; v++){
		attr = (usize*)((char*)v + aoff);
		for(reidx = t->tab; reidx < t->tab+t->len; reidx++)
			if(*attr == reidx->old)
				*attr = reidx->new;
			else if(*attr > reidx->old)
				(*attr)--;
	}
}

static void
reindexprimtans(ItemArray *prims, Reidxtab *t)
{
	Reidx *reidx;
	Primitive *P, *Pb, *Pe;

	if(t->len == 0)
		return;

	Pb = prims->items;
	Pe = Pb + prims->nitems;

	for(P = Pb; P < Pe; P++)
	for(reidx = t->tab; reidx < t->tab+t->len; reidx++)
		if(P->tangent == reidx->old)
			P->tangent = reidx->new;
		else if(P->tangent > reidx->old)
			P->tangent--;
}

static void
reindexprimverts(ItemArray *prims, Reidxtab *t)
{
	Reidx *reidx;
	Primitive *P, *Pb, *Pe;
	usize i;

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
dedupitemarray(ItemArray *a, Reidxtab *t)
{
	char *p1, *p2, *pb, *pe;
	void *vp;
	usize nitems0, i, j;

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

	dedupitemarray(m->positions, &itab);
	reindexverts(m->verts, &itab, offsetof(Vertex, p));
	itab.len = 0;

	dedupitemarray(m->normals, &itab);
	reindexverts(m->verts, &itab, offsetof(Vertex, n));
	itab.len = 0;

	dedupitemarray(m->texcoords, &itab);
	reindexverts(m->verts, &itab, offsetof(Vertex, uv));
	itab.len = 0;

	dedupitemarray(m->colors, &itab);
	reindexverts(m->verts, &itab, offsetof(Vertex, c));
	itab.len = 0;

	dedupitemarray(m->tangents, &itab);
	reindexprimtans(m->prims, &itab);
	itab.len = 0;

	dedupitemarray(m->verts, &itab);
	reindexprimverts(m->prims, &itab);

	dedupitemarray(m->prims, nil);

	freereidxtab(&itab);
}
