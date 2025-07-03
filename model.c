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

void
compactmodel(Model *m)
{
	ItemArray *a1, *a2;
	Point3 *p1, *p2, *pb, *pe;
	Point2 *t1, *t2, *tb, *te;
	Vertex *v, *v1, *v2, *vb, *ve;
	Primitive *P, *P1, *P2, *Pb, *Pe;
	void *vp;
	usize len0, i, j, k;

	a1 = m->positions;
	pb = a1->items;
	pe = pb + a1->nitems;
	len0 = a1->nitems;
	a2 = m->verts;
	vb = a2->items;
	ve = vb + a2->nitems;

	for(p1 = pb, i = 0; p1 < pe; p1++, i++)
	for(p2 = p1+1, j = i+1; p2 < pe; p2++, j++)
		if(memcmp(p1, p2, sizeof(Point3)) == 0){
			if(p2 < --pe)
				memmove(p2, p2+1, (pe - p2)*sizeof(Point3));
			/* reindex verts */
			for(v = vb; v < ve; v++)
				if(v->p == j)
					v->p = i;
				else if(v->p > j)
					v->p--;
		}
	a1->nitems = pe - pb;
	if(a1->nitems != len0){
		vp = realloc(a1->items, a1->nitems * a1->itemsize);
		if(vp == nil)
			fprint(2, "not enough memory to shrink positions array.\n");
		else
			a1->items = vp;
	}

	a1 = m->normals;
	pb = a1->items;
	pe = pb + a1->nitems;
	len0 = a1->nitems;

	for(p1 = pb, i = 0; p1 < pe; p1++, i++)
	for(p2 = p1+1, j = i+1; p2 < pe; p2++, j++)
		if(memcmp(p1, p2, sizeof(Point3)) == 0){
			if(p2 < --pe)
				memmove(p2, p2+1, (pe - p2)*sizeof(Point3));
			/* reindex verts */
			for(v = vb; v < ve; v++)
				if(v->n == j)
					v->n = i;
				else if(v->n > j)
					v->n--;
		}
	a1->nitems = pe - pb;
	if(a1->nitems != len0){
		vp = realloc(a1->items, a1->nitems * a1->itemsize);
		if(vp == nil)
			fprint(2, "not enough memory to shrink normals array.\n");
		else
			a1->items = vp;
	}

	a1 = m->texcoords;
	tb = a1->items;
	te = tb + a1->nitems;
	len0 = a1->nitems;

	for(t1 = tb, i = 0; t1 < te; t1++, i++)
	for(t2 = t1+1, j = i+1; t2 < te; t2++, j++)
		if(memcmp(t1, t2, sizeof(Point2)) == 0){
			if(t2 < --te)
				memmove(t2, t2+1, (te - t2)*sizeof(Point2));
			/* reindex verts */
			for(v = vb; v < ve; v++)
				if(v->uv == j)
					v->uv = i;
				else if(v->uv > j)
					v->uv--;
		}
	a1->nitems = te - tb;
	if(a1->nitems != len0){
		vp = realloc(a1->items, a1->nitems * a1->itemsize);
		if(vp == nil)
			fprint(2, "not enough memory to shrink texcoords array.\n");
		else
			a1->items = vp;
	}

	a1 = m->colors;
	pb = a1->items;
	pe = pb + a1->nitems;
	len0 = a1->nitems;

	for(p1 = pb, i = 0; p1 < pe; p1++, i++)
	for(p2 = p1+1, j = i+1; p2 < pe; p2++, j++)
		if(memcmp(p1, p2, sizeof(Point3)) == 0){
			if(p2 < --pe)
				memmove(p2, p2+1, (pe - p2)*sizeof(Point3));
			/* reindex verts */
			for(v = vb; v < ve; v++)
				if(v->c == j)
					v->c = i;
				else if(v->c > j)
					v->c--;
		}
	a1->nitems = pe - pb;
	if(a1->nitems != len0){
		vp = realloc(a1->items, a1->nitems * a1->itemsize);
		if(vp == nil)
			fprint(2, "not enough memory to shrink colors array.\n");
		else
			a1->items = vp;
	}

	a1 = m->tangents;
	pb = a1->items;
	pe = pb + a1->nitems;
	len0 = a1->nitems;
	a2 = m->prims;
	Pb = a2->items;
	Pe = Pb + a2->nitems;

	for(p1 = pb, i = 0; p1 < pe; p1++, i++)
	for(p2 = p1+1, j = i+1; p2 < pe; p2++, j++)
		if(memcmp(p1, p2, sizeof(Point3)) == 0){
			if(p2 < --pe)
				memmove(p2, p2+1, (pe - p2)*sizeof(Point3));
			/* reindex prims */
			for(P = Pb; P < Pe; P++)
				if(P->tangent == j)
					P->tangent = i;
				else if(P->tangent > j)
					P->tangent--;
		}
	a1->nitems = pe - pb;
	if(a1->nitems != len0){
		vp = realloc(a1->items, a1->nitems * a1->itemsize);
		if(vp == nil)
			fprint(2, "not enough memory to shrink tangents array.\n");
		else
			a1->items = vp;
	}

	a1 = m->verts;
	vb = a1->items;
	ve = vb + a1->nitems;
	len0 = a1->nitems;

	for(v1 = vb, i = 0; v1 < ve; v1++, i++)
	for(v2 = v1+1, j = i+1; v2 < ve; v2++, j++)
		if(memcmp(v1, v2, sizeof(Vertex)) == 0){
			if(v2 < --ve)
				memmove(v2, v2+1, (ve - v2)*sizeof(Vertex));
			/* reindex prims */
			for(P = Pb; P < Pe; P++)
				for(k = 0; k < P->type+1; k++)
					if(P->v[k] == j)
						P->v[k] = i;
					else if(P->v[k] > j)
						P->v[k]--;
		}
	a1->nitems = ve - vb;
	if(a1->nitems != len0){
		vp = realloc(a1->items, a1->nitems * a1->itemsize);
		if(vp == nil)
			fprint(2, "not enough memory to shrink vertex array.\n");
		else
			a1->items = vp;
	}

	len0 = a2->nitems;
	for(P1 = Pb, i = 0; P1 < Pe; P1++, i++)
	for(P2 = P1+1, j = i+1; P2 < Pe; P2++, j++)
		if(memcmp(P1, P2, sizeof(Primitive)) == 0)
			if(P2 < --Pe)
				memmove(P2, P2+1, (Pe - P2)*sizeof(Primitive));
	a2->nitems = Pe - Pb;
	if(a2->nitems != len0){
		vp = realloc(a2->items, a2->nitems * a2->itemsize);
		if(vp == nil)
			fprint(2, "not enough memory to shrink vertex array.\n");
		else
			a2->items = vp;
	}
}
