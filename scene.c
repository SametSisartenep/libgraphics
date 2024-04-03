#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

/*
 * fan triangulation.
 *
 * TODO check that the polygon is in fact convex
 * try to adapt if not (by finding a convex
 * vertex), or discard it.
 */
static int
triangulate(OBJElem **newe, OBJElem *e)
{
	OBJIndexArray *newidxtab;
	OBJIndexArray *idxtab;
	int i, nt;

	nt = 0;
	idxtab = &e->indextab[OBJVGeometric];
	for(i = 0; i < idxtab->nindex-2; i++){
		idxtab = &e->indextab[OBJVGeometric];
		newe[nt++] = emalloc(sizeof **newe);
		newe[nt-1]->type = OBJEFace;
		newe[nt-1]->mtl = e->mtl;
		newidxtab = &newe[nt-1]->indextab[OBJVGeometric];
		newidxtab->nindex = 3;
		newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
		newidxtab->indices[0] = idxtab->indices[0];
		newidxtab->indices[1] = idxtab->indices[i+1];
		newidxtab->indices[2] = idxtab->indices[i+2];
		idxtab = &e->indextab[OBJVTexture];
		if(idxtab->nindex > 0){
			newidxtab = &newe[nt-1]->indextab[OBJVTexture];
			newidxtab->nindex = 3;
			newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
			newidxtab->indices[0] = idxtab->indices[0];
			newidxtab->indices[1] = idxtab->indices[i+1];
			newidxtab->indices[2] = idxtab->indices[i+2];
		}
		idxtab = &e->indextab[OBJVNormal];
		if(idxtab->nindex > 0){
			newidxtab = &newe[nt-1]->indextab[OBJVNormal];
			newidxtab->nindex = 3;
			newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
			newidxtab->indices[0] = idxtab->indices[0];
			newidxtab->indices[1] = idxtab->indices[i+1];
			newidxtab->indices[2] = idxtab->indices[i+2];
		}
	}

	return nt;
}

/*
 * rebuild the cache of renderable OBJElems.
 *
 * run it every time the Model's geometry changes.
 */
int
refreshmodel(Model *m)
{
	OBJElem **trielems;
	OBJObject *o;
	OBJElem *e;
	OBJIndexArray *idxtab;
	int i, nt;

	if(m->obj == nil)
		return 0;

	if(m->elems != nil){
		free(m->elems);
		m->elems = nil;
	}
	m->nelems = 0;
	for(i = 0; i < nelem(m->obj->objtab); i++)
		for(o = m->obj->objtab[i]; o != nil; o = o->next)
			for(e = o->child; e != nil; e = e->next){
				idxtab = &e->indextab[OBJVGeometric];
				/* discard non-surfaces */
				if(e->type != OBJEFace || idxtab->nindex < 3)
					continue;
				if(idxtab->nindex > 3){
					/* it takes n-2 triangles to fill any given n-gon */
					trielems = emalloc((idxtab->nindex-2)*sizeof(*trielems));
					nt = triangulate(trielems, e);
					m->nelems += nt;
					m->elems = erealloc(m->elems, m->nelems*sizeof(*m->elems));
					while(nt-- > 0)
						m->elems[m->nelems-nt-1] = trielems[nt];
					free(trielems);
				}else{
					m->elems = erealloc(m->elems, ++m->nelems*sizeof(*m->elems));
					m->elems[m->nelems-1] = e;
				}
			}
	return m->nelems;
}

Model *
newmodel(void)
{
	Model *m;

	m = emalloc(sizeof *m);
	memset(m, 0, sizeof *m);
	return m;
}

void
delmodel(Model *m)
{
	if(m == nil)
		return;
	if(m->obj != nil)
		objfree(m->obj);
	if(m->tex != nil)
		freememimage(m->tex);
	if(m->nor != nil)
		freememimage(m->nor);
	if(m->nelems > 0)
		free(m->elems);
	free(m);
}

Entity *
newentity(Model *m)
{
	Entity *e;

	e = emalloc(sizeof *e);
	e->p = Pt3(0,0,0,1);
	e->bx = Vec3(1,0,0);
	e->by = Vec3(0,1,0);
	e->bz = Vec3(0,0,1);
	e->mdl = m;
	e->prev = e->next = nil;
	return e;
}

void
delentity(Entity *e)
{
	if(e == nil)
		return;
	if(e->mdl != nil)
		delmodel(e->mdl);
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

Scene *
newscene(char *name)
{
	Scene *s;

	s = emalloc(sizeof *s);
	s->name = name == nil? nil: strdup(name);
	s->ents.prev = s->ents.next = &s->ents;
	s->nents = 0;
	s->addent = scene_addent;
	s->delent = scene_delent;
	return s;
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

void
clearscene(Scene *s)
{
	Entity *e;

	for(e = s->ents.next; e != &s->ents; e = e->next){
		s->delent(s, e);
		delentity(e);
	}
}
