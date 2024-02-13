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
 * it only processes quads for now.
 */
static int
triangulate(OBJElem **newe, OBJElem *e)
{
	OBJIndexArray *newidxtab;
	OBJIndexArray *idxtab;

	idxtab = &e->indextab[OBJVGeometric];
	newe[0] = emalloc(sizeof **newe);
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
	newe[1] = emalloc(sizeof **newe);
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

/*
 * rebuild the cache of renderable OBJElems.
 *
 * run it every time the Model's geometry changes.
 */
int
refreshmodel(Model *m)
{
	OBJElem *trielems[2];
	OBJObject *o;
	OBJElem *e;
	OBJIndexArray *idxtab;
	int i;

	if(m->elems != nil){
		free(m->elems);
		m->elems = nil;
	}
	m->nelems = 0;
	for(i = 0; i < nelem(m->obj->objtab); i++)
		for(o = m->obj->objtab[i]; o != nil; o = o->next)
			for(e = o->child; e != nil; e = e->next){
				idxtab = &e->indextab[OBJVGeometric];
				/* discard non-triangles */
				if(e->type != OBJEFace || (idxtab->nindex != 3 && idxtab->nindex != 4))
					continue;
				if(idxtab->nindex == 4){
					triangulate(trielems, e);
					m->nelems += 2;
					m->elems = erealloc(m->elems, m->nelems*sizeof(*m->elems));
					m->elems[m->nelems-2] = trielems[0];
					m->elems[m->nelems-1] = trielems[1];
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
	/* TODO free model */
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

Scene *
newscene(char *name)
{
	Scene *s;

	s = emalloc(sizeof *s);
	s->name = name == nil? nil: strdup(name);
	s->ents.prev = s->ents.next = &s->ents;
	s->addent = scene_addent;
	return s;
}

void
delscene(Scene *s)
{
	/* TODO free ents */
	free(s->name);
	free(s);
}
