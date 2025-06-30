#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

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
