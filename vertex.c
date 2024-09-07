#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

static void
_addvattr(Vertex *v, Vertexattr *va)
{
	int i;

	assert(va->id != nil);

	for(i = 0; i < v->nattrs; i++)
		if(strcmp(v->attrs[i].id, va->id) == 0){
			v->attrs[i] = *va;
			return;
		}
	v->attrs = erealloc(v->attrs, ++v->nattrs*sizeof(*va));
	v->attrs[v->nattrs-1] = *va;
}

static void
copyvattrs(Vertex *d, Vertex *s)
{
	int i;

	for(i = 0; i < s->nattrs; i++)
		_addvattr(d, &s->attrs[i]);
}

Vertex
dupvertex(Vertex *v)
{
	Vertex nv;

	nv = *v;
	nv.attrs = nil;
	nv.nattrs = 0;
	copyvattrs(&nv, v);
	return nv;
}

/*
 * linear attribute interpolation
 */
void
lerpvertex(Vertex *v, Vertex *v0, Vertex *v1, double t)
{
	Vertexattr va;
	int i;

	v->p = lerp3(v0->p, v1->p, t);
	v->n = lerp3(v0->n, v1->n, t);
	v->c = lerp3(v0->c, v1->c, t);
	v->uv = lerp2(v0->uv, v1->uv, t);
	v->mtl = v0->mtl != nil? v0->mtl: v1->mtl;
	for(i = 0; i < v0->nattrs; i++){
		va.id = v0->attrs[i].id;
		va.type = v0->attrs[i].type;
		if(va.type == VAPoint)
			va.p = lerp3(v0->attrs[i].p, v1->attrs[i].p, t);
		else
			va.n = flerp(v0->attrs[i].n, v1->attrs[i].n, t);
		_addvattr(v, &va);
	}
}

/*
 * barycentric attribute interpolation
 */
void
berpvertex(Vertex *v, Vertex *v0, Vertex *v1, Vertex *v2, Point3 bc)
{
	Vertexattr va;
	int i;

	v->p = berp3(v0->p, v1->p, v2->p, bc);
	v->n = berp3(v0->n, v1->n, v2->n, bc);
	v->c = berp3(v0->c, v1->c, v2->c, bc);
	v->uv = berp2(v0->uv, v1->uv, v2->uv, bc);
	v->mtl = v0->mtl != nil? v0->mtl: v1->mtl != nil? v1->mtl: v2->mtl;
	for(i = 0; i < v0->nattrs; i++){
		va.id = v0->attrs[i].id;
		va.type = v0->attrs[i].type;
		if(va.type == VAPoint)
			va.p = berp3(v0->attrs[i].p, v1->attrs[i].p, v2->attrs[i].p, bc);
		else
			va.n = fberp(v0->attrs[i].n, v1->attrs[i].n, v2->attrs[i].n, bc);
		_addvattr(v, &va);
	}
}

void
addvattr(Vertex *v, char *id, int type, void *val)
{
	Vertexattr va;

	va.id = id;
	va.type = type;
	switch(type){
	case VAPoint: va.p = *(Point3*)val; break;
	case VANumber: va.n = *(double*)val; break;
	default: sysfatal("unknown vertex attribute type '%d'", type);
	}
	_addvattr(v, &va);
}

Vertexattr *
getvattr(Vertex *v, char *id)
{
	int i;

	for(i = 0; i < v->nattrs; i++)
		if(id != nil && strcmp(v->attrs[i].id, id) == 0)
			return &v->attrs[i];
	return nil;
}

void
delvattrs(Vertex *v)
{
	free(v->attrs);
	v->attrs= nil;
	v->nattrs = 0;
}

void
fprintvattrs(int fd, Vertex *v)
{
	int i;

	for(i = 0; i < v->nattrs; i++)
		fprint(fd, "id %s type %d v %g\n",
			v->attrs[i].id, v->attrs[i].type, v->attrs[i].n);
}
