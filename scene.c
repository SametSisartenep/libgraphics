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
	int i;

	idxtab = &e->indextab[OBJVGeometric];
	for(i = 0; i < idxtab->nindex-2; i++){
		idxtab = &e->indextab[OBJVGeometric];
		newe[i] = emalloc(sizeof **newe);
		memset(newe[i], 0, sizeof **newe);
		newe[i]->type = OBJEFace;
		newe[i]->mtl = e->mtl;
		newidxtab = &newe[i]->indextab[OBJVGeometric];
		newidxtab->nindex = 3;
		newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
		newidxtab->indices[0] = idxtab->indices[0];
		newidxtab->indices[1] = idxtab->indices[i+1];
		newidxtab->indices[2] = idxtab->indices[i+2];
		idxtab = &e->indextab[OBJVTexture];
		if(idxtab->nindex > 0){
			newidxtab = &newe[i]->indextab[OBJVTexture];
			newidxtab->nindex = 3;
			newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
			newidxtab->indices[0] = idxtab->indices[0];
			newidxtab->indices[1] = idxtab->indices[i+1];
			newidxtab->indices[2] = idxtab->indices[i+2];
		}
		idxtab = &e->indextab[OBJVNormal];
		if(idxtab->nindex > 0){
			newidxtab = &newe[i]->indextab[OBJVNormal];
			newidxtab->nindex = 3;
			newidxtab->indices = emalloc(newidxtab->nindex*sizeof(*newidxtab->indices));
			newidxtab->indices[0] = idxtab->indices[0];
			newidxtab->indices[1] = idxtab->indices[i+1];
			newidxtab->indices[2] = idxtab->indices[i+2];
		}
	}

	return i;
}

typedef struct OBJ2MtlEntry OBJ2MtlEntry;
typedef struct OBJ2MtlMap OBJ2MtlMap;

struct OBJ2MtlEntry
{
	OBJMaterial *objmtl;
	ulong idx;
	OBJ2MtlEntry *next;
};

struct OBJ2MtlMap
{
	OBJ2MtlEntry *head;
	Material *mtls;
};

static void
addmtlmap(OBJ2MtlMap *map, OBJMaterial *om, ulong idx)
{
	OBJ2MtlEntry *e;

	if(om == nil)
		return;

	e = emalloc(sizeof *e);
	memset(e, 0, sizeof *e);
	e->objmtl = om;
	e->idx = idx;

	if(map->head == nil){
		map->head = e;
		return;
	}

	e->next = map->head;
	map->head = e;
}

static Material *
getmtlmap(OBJ2MtlMap *map, OBJMaterial *om)
{
	OBJ2MtlEntry *e;

	for(e = map->head; e != nil; e = e->next)
		if(e->objmtl == om)
			return &map->mtls[e->idx];
	return nil;
}

static void
clrmtlmap(OBJ2MtlMap *map)
{
	OBJ2MtlEntry *e, *ne;

	for(e = map->head; e != nil; e = ne){
		ne = e->next;
		free(e);
	}
}

int
loadobjmodel(Model *m, OBJ *obj)
{
	Primitive *p;
	OBJVertex *pverts, *tverts, *nverts, *v;	/* geometric, texture and normals vertices */
	OBJElem **trielems, *e, *ne;
	OBJObject *o;
	OBJIndexArray *idxtab;
	OBJ2MtlMap mtlmap;
	OBJMaterial *objmtl;
	Material *mtl;
	Point3 n;					/* surface normal */
	int i, idx, nt, maxnt, neednormal, gottaclean;

	if(obj == nil)
		return 0;

	pverts = obj->vertdata[OBJVGeometric].verts;
	tverts = obj->vertdata[OBJVTexture].verts;
	nverts = obj->vertdata[OBJVNormal].verts;
	trielems = nil;
	maxnt = 0;

	if(m->prims != nil){
		free(m->prims);
		m->prims = nil;
	}
	m->nprims = 0;

	mtlmap.head = nil;
	for(i = 0; obj->materials != nil && i < nelem(obj->materials->mattab); i++)
		for(objmtl = obj->materials->mattab[i]; objmtl != nil; objmtl = objmtl->next){
			mtlmap.mtls = m->materials = erealloc(m->materials, ++m->nmaterials*sizeof(*m->materials));
			mtl = &m->materials[m->nmaterials-1];
			memset(mtl, 0, sizeof *mtl);

			if(objmtl->name != nil){
				mtl->name = strdup(objmtl->name);
				if(mtl->name == nil)
					sysfatal("strdup: %r");
			}
			mtl->ambient.r = objmtl->Ka.r;
			mtl->ambient.g = objmtl->Ka.g;
			mtl->ambient.b = objmtl->Ka.b;
			mtl->ambient.a = 1;
			mtl->diffuse.r = objmtl->Kd.r;
			mtl->diffuse.g = objmtl->Kd.g;
			mtl->diffuse.b = objmtl->Kd.b;
			mtl->diffuse.a = 1;
			mtl->specular.r = objmtl->Ks.r;
			mtl->specular.g = objmtl->Ks.g;
			mtl->specular.b = objmtl->Ks.b;
			mtl->specular.a = 1;
			mtl->shininess = objmtl->Ns;

			if(objmtl->map_Kd != nil){
				mtl->diffusemap = allocmemimaged(objmtl->map_Kd->r, objmtl->map_Kd->chan, objmtl->map_Kd->data);
				if(mtl->diffusemap == nil)
					sysfatal("allocmemimaged: %r");
				mtl->diffusemap->data->ref++;
			}

			if(objmtl->norm != nil){
				mtl->normalmap = allocmemimaged(objmtl->norm->r, objmtl->norm->chan, objmtl->norm->data);
				if(mtl->normalmap == nil)
					sysfatal("allocmemimaged: %r");
				mtl->normalmap->data->ref++;
			}

			addmtlmap(&mtlmap, objmtl, m->nmaterials-1);
		}

	for(i = 0; i < nelem(obj->objtab); i++)
		for(o = obj->objtab[i]; o != nil; o = o->next)
			for(e = o->child; e != nil; e = ne){
				ne = e->next;

				switch(e->type){
				case OBJEPoint:
					m->prims = erealloc(m->prims, ++m->nprims*sizeof(*m->prims));
					p = &m->prims[m->nprims-1];
					memset(p, 0, sizeof *p);
					p->type = PPoint;
					p->mtl = getmtlmap(&mtlmap, e->mtl);

					idxtab = &e->indextab[OBJVGeometric];
					v = &pverts[idxtab->indices[0]];
					p->v[0].p = Pt3(v->x, v->y, v->z, v->w);

					idxtab = &e->indextab[OBJVTexture];
					if(idxtab->nindex == 1){
						v = &tverts[idxtab->indices[0]];
						p->v[0].uv = Pt2(v->u, v->v, 1);
					}
					break;
				case OBJELine:
					m->prims = erealloc(m->prims, ++m->nprims*sizeof(*m->prims));
					p = &m->prims[m->nprims-1];
					memset(p, 0, sizeof *p);
					p->type = PLine;
					p->mtl = getmtlmap(&mtlmap, e->mtl);

					for(idx = 0; idx < 2; idx++){
						idxtab = &e->indextab[OBJVGeometric];
						v = &pverts[idxtab->indices[idx]];
						p->v[idx].p = Pt3(v->x, v->y, v->z, v->w);

						idxtab = &e->indextab[OBJVTexture];
						if(idxtab->nindex == 2){
							v = &tverts[idxtab->indices[idx]];
							p->v[idx].uv = Pt2(v->u, v->v, 1);
						}
					}
					break;
				case OBJEFace:
					idxtab = &e->indextab[OBJVGeometric];
					assert(idxtab->nindex >= 3);
					gottaclean = 0;

					/* it takes n-2 triangles to fill any given n-gon */
					nt = idxtab->nindex-2;
					if(nt > maxnt){
						maxnt = nt;
						trielems = erealloc(trielems, maxnt*sizeof(*trielems));
					}
					if(nt > 1){
						assert(triangulate(trielems, e) == nt);
						gottaclean = 1;
					}else
						trielems[0] = e;

					while(nt-- > 0){
						e = trielems[nt];
						neednormal = 0;

						m->prims = erealloc(m->prims, ++m->nprims*sizeof(*m->prims));
						p = &m->prims[m->nprims-1];
						memset(p, 0, sizeof *p);
						p->type = PTriangle;
						p->mtl = getmtlmap(&mtlmap, e->mtl);

						for(idx = 0; idx < 3; idx++){
							idxtab = &e->indextab[OBJVGeometric];
							v = &pverts[idxtab->indices[idx]];
							p->v[idx].p = Pt3(v->x, v->y, v->z, v->w);

							idxtab = &e->indextab[OBJVNormal];
							if(idxtab->nindex == 3){
								v = &nverts[idxtab->indices[idx]];
								p->v[idx].n = normvec3(Vec3(v->i, v->j, v->k));
							}else
								neednormal = 1;

							idxtab = &e->indextab[OBJVTexture];
							if(idxtab->nindex == 3){
								v = &tverts[idxtab->indices[idx]];
								p->v[idx].uv = Pt2(v->u, v->v, 1);
							}
						}
						if(p->v[0].uv.w != 0){
							Point3 e0, e1;
							Point2 Δuv0, Δuv1;
							double det;

							e0 = subpt3(p->v[1].p, p->v[0].p);
							e1 = subpt3(p->v[2].p, p->v[0].p);
							Δuv0 = subpt2(p->v[1].uv, p->v[0].uv);
							Δuv1 = subpt2(p->v[2].uv, p->v[0].uv);

							det = Δuv0.x * Δuv1.y - Δuv1.x * Δuv0.y;
							det = det == 0? 0: 1.0/det;
							p->tangent.x = det*(Δuv1.y * e0.x - Δuv0.y * e1.x);
							p->tangent.y = det*(Δuv1.y * e0.y - Δuv0.y * e1.y);
							p->tangent.z = det*(Δuv1.y * e0.z - Δuv0.y * e1.z);
							p->tangent = normvec3(p->tangent);
						}
						if(neednormal){
							/* TODO build a list of per-vertex normals earlier */
							n = normvec3(crossvec3(subpt3(p->v[1].p, p->v[0].p), subpt3(p->v[2].p, p->v[0].p)));
							p->v[0].n = p->v[1].n = p->v[2].n = n;
						}
						if(gottaclean){
							free(e->indextab[OBJVGeometric].indices);
							free(e->indextab[OBJVNormal].indices);
							free(e->indextab[OBJVTexture].indices);
							free(e);
						}
					}
					break;
				default: continue;
				}
			}

	free(trielems);
	clrmtlmap(&mtlmap);
	return m->nprims;
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
	if(m->tex != nil)
		freememimage(m->tex);
	if(m->nmaterials > 0){
		while(m->nmaterials--){
			freememimage(m->materials[m->nmaterials].diffusemap);
			freememimage(m->materials[m->nmaterials].normalmap);
			free(m->materials[m->nmaterials].name);
		}
		free(m->materials);
	}
	if(m->nprims > 0)
		free(m->prims);
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
	Entity *e, *ne;

	for(e = s->ents.next; e != &s->ents; e = ne){
		ne = e->next;
		s->delent(s, e);
		delentity(e);
	}
}
