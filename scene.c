#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

static int
model_addprim(Model *m, Primitive p)
{
	m->prims = erealloc(m->prims, ++m->nprims*sizeof(*m->prims));
	m->prims[m->nprims-1] = p;
	return m->nprims-1;
}

static int
model_addmaterial(Model *m, Material mtl)
{
	m->materials = erealloc(m->materials, ++m->nmaterials*sizeof(*m->materials));
	m->materials[m->nmaterials-1] = mtl;
	return m->nmaterials-1;
}

Model *
newmodel(void)
{
	Model *m;

	m = emalloc(sizeof *m);
	memset(m, 0, sizeof *m);
	m->addprim = model_addprim;
	m->addmaterial = model_addmaterial;
	return m;
}

Model *
dupmodel(Model *m)
{
	Model *nm;
	int i;

	if(m == nil)
		return nil;

	nm = newmodel();
	nm->tex = duptexture(m->tex);
	if(m->nmaterials > 0){
		nm->nmaterials = m->nmaterials;
		nm->materials = emalloc(nm->nmaterials*sizeof(*nm->materials));
		for(i = 0; i < m->nmaterials; i++){
			nm->materials[i] = m->materials[i];
			nm->materials[i].diffusemap = duptexture(m->materials[i].diffusemap);
			nm->materials[i].specularmap = duptexture(m->materials[i].specularmap);
			nm->materials[i].normalmap = duptexture(m->materials[i].normalmap);
			nm->materials[i].name = strdup(m->materials[i].name);
			if(nm->materials[i].name == nil)
				sysfatal("strdup: %r");
		}
	}
	if(m->nprims > 0){
		nm->nprims = m->nprims;
		nm->prims = emalloc(nm->nprims*sizeof(*nm->prims));
		for(i = 0; i < m->nprims; i++){
			nm->prims[i] = m->prims[i];
			if(nm->nmaterials > 0 && m->prims[i].mtl != nil)
				nm->prims[i].mtl = &nm->materials[m->prims[i].mtl - m->materials];
		}
	}
	return nm;
}

void
delmodel(Model *m)
{
	if(m == nil)
		return;

	freetexture(m->tex);
	while(m->nmaterials--){
		freetexture(m->materials[m->nmaterials].diffusemap);
		freetexture(m->materials[m->nmaterials].specularmap);
		freetexture(m->materials[m->nmaterials].normalmap);
		free(m->materials[m->nmaterials].name);
	}
	free(m->materials);
	free(m->prims);
	free(m);
}

Entity *
newentity(char *name, Model *m)
{
	Entity *e;

	e = emalloc(sizeof *e);
	e->p = Pt3(0,0,0,1);
	e->bx = Vec3(1,0,0);
	e->by = Vec3(0,1,0);
	e->bz = Vec3(0,0,1);
	e->name = name == nil? nil: strdup(name);
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
		ne->name = strdup(e->name);
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

	s = emalloc(sizeof *s);
	s->name = name == nil? nil: strdup(name);
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

	if(s == nil)
		return nil;

	ns = newscene(s->name);
	if(s->nents > 0)
		for(e = s->ents.next; e != &s->ents; e = e->next)
			ns->addent(ns, dupentity(e));
	ns->skybox = dupcubemap(s->skybox);
	return ns;
}

void
clearscene(Scene *s)
{
	Entity *e, *ne;

	for(e = s->ents.next; e != &s->ents; e = ne){
		ne = e->next;
		s->delent(s, e);
		delentity(e);
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
