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
	return itemarrayadd(m->positions, &p, 0);
}

static usize
model_addnormal(Model *m, Point3 n)
{
	return itemarrayadd(m->normals, &n, 0);
}

static usize
model_addtexcoord(Model *m, Point2 t)
{
	return itemarrayadd(m->texcoords, &t, 0);
}

static usize
model_addcolor(Model *m, Color c)
{
	return itemarrayadd(m->colors, &c, 0);
}

static usize
model_addtangent(Model *m, Point3 T)
{
	return itemarrayadd(m->tangents, &T, 0);
}

static usize
model_addvert(Model *m, Vertex v)
{
	return itemarrayadd(m->verts, &v, 0);
}

static usize
model_addprim(Model *m, Primitive P)
{
	return itemarrayadd(m->prims, &P, 0);
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

static LightSource *
newlight(int t, Point3 p, Point3 d, Color c, double coff, double θu, double θp)
{
	LightSource *l;

	l = _emalloc(sizeof *l);
	l->type = t;
	l->p = p;
	l->dir = d;
	l->c = c;
	l->cutoff = coff;
	l->θu = θu;
	l->θp = θp;
	l->prev = l->next = nil;
	return l;
}

LightSource *
newpointlight(Point3 p, Color c)
{
	return newlight(LightPoint, p, ZP3, c, 1000, 0, 0);
}

LightSource *
newdireclight(Point3 p, Point3 dir, Color c)
{
	return newlight(LightDirectional, p, dir, c, 1000, 0, 0);
}

LightSource *
newspotlight(Point3 p, Point3 dir, Color c, double θu, double θp)
{
	return newlight(LightSpot, p, dir, c, 1000, θu, θp);
}

LightSource *
duplight(LightSource *l)
{
	LightSource *nl;

	nl = _emalloc(sizeof *nl);
	memset(nl, 0, sizeof *nl);
	*nl = *l;
	nl->prev = nl->next = nil;
	return nl;
}

void
dellight(LightSource *l)
{
	free(l);
}

Entity *
newentity(char *name, Model *m)
{
	Entity *e;

	e = _emalloc(sizeof *e);
	e->p = Pt3(0,0,0,1);
	e->bx = Vec3(1,0,0);
	e->by = Vec3(0,1,0);
	e->bz = Vec3(0,0,1);
	e->name = name == nil? nil: _estrdup(name);
	e->mdl = m;
	e->prev = e->next = nil;
	return e;
}

Entity *
dupentity(Entity *e)
{
	Entity *ne;

	if(e == nil)
		return nil;

	ne = newentity(nil, nil);
	*ne = *e;
	if(e->name != nil)
		ne->name = _estrdup(e->name);
	ne->mdl = dupmodel(e->mdl);
	ne->prev = ne->next = nil;
	return ne;
}

void
delentity(Entity *e)
{
	if(e == nil)
		return;

	delmodel(e->mdl);
	free(e->name);
	free(e);
}

static void
scene_addent(Scene *s, Entity *e)
{
	e->prev = s->ents.prev;
	e->next = s->ents.prev->next;
	s->ents.prev->next = e;
	s->ents.prev = e;
	s->nents++;
}

static void
scene_delent(Scene *s, Entity *e)
{
	e->prev->next = e->next;
	e->next->prev = e->prev;
	e->prev = e->next = nil;
	s->nents--;
}

static Entity *
scene_getent(Scene *s, char *name)
{
	Entity *e;

	for(e = s->ents.next; e != &s->ents; e = e->next)
		if(strcmp(e->name, name) == 0)
			return e;
	return nil;
}

static void
scene_addlight(Scene *s, LightSource *l)
{
	l->prev = s->lights.prev;
	l->next = s->lights.prev->next;
	s->lights.prev->next = l;
	s->lights.prev = l;
	s->nlights++;
}

Scene *
newscene(char *name)
{
	Scene *s;

	s = _emalloc(sizeof *s);
	s->name = name == nil? nil: _estrdup(name);
	s->ents.prev = s->ents.next = &s->ents;
	s->nents = 0;
	s->lights.prev = s->lights.next = &s->lights;
	s->nlights = 0;
	s->skybox = nil;
	s->addent = scene_addent;
	s->delent = scene_delent;
	s->getent = scene_getent;
	s->addlight = scene_addlight;
	return s;
}

Scene *
dupscene(Scene *s)
{
	Scene *ns;
	Entity *e;
	LightSource *l;

	if(s == nil)
		return nil;

	ns = newscene(s->name);
	for(e = s->ents.next; e != &s->ents; e = e->next)
		ns->addent(ns, dupentity(e));
	for(l = s->lights.next; l != &s->lights; l = l->next)
		ns->addlight(ns, duplight(l));
	ns->skybox = dupcubemap(s->skybox);
	return ns;
}

void
clearscene(Scene *s)
{
	Entity *e, *ne;
	LightSource *l, *nl;

	for(e = s->ents.next; e != &s->ents; e = ne){
		ne = e->next;
		s->delent(s, e);
		delentity(e);
	}
	for(l = s->lights.next; l != &s->lights; l = nl){
		nl = l->next;
		dellight(l);
	}
	freecubemap(s->skybox);
}

void
delscene(Scene *s)
{
	if(s == nil)
		return;

	clearscene(s);
	free(s->name);
	free(s);
}
