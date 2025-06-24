#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

static void
addvattr(Vertexattrs *v, Vertexattr *va)
{
	int i;

	assert(va->id != nil);

	for(i = 0; i < v->nattrs; i++)
		if(strcmp(v->attrs[i].id, va->id) == 0){
			v->attrs[i] = *va;
			return;
		}
	if(v->nattrs % 8 == 0)
		v->attrs = _erealloc(v->attrs, (v->nattrs + 8)*sizeof(*va));
	v->attrs[v->nattrs++] = *va;
}

static void
copyvattrs(BVertex *d, BVertex *s)
{
	int i;

	for(i = 0; i < s->nattrs; i++)
		addvattr(d, &s->attrs[i]);
}

BVertex
_dupvertex(BVertex *v)
{
	BVertex nv;

	nv = *v;
	nv.attrs = nil;
	nv.nattrs = 0;
	copyvattrs(&nv, v);
	return nv;
}

void
_loadvertex(BVertex *d, BVertex *s)
{
	d->p = s->p;
	d->n = s->n;
	d->c = s->c;
	d->uv = s->uv;
	d->tangent = s->tangent;
	d->mtl = s->mtl;
	copyvattrs(d, s);
}

/*
 * linear attribute interpolation
 */
void
_lerpvertex(BVertex *v, BVertex *v0, BVertex *v1, double t)
{
	Vertexattr va;
	int i;

	v->p = lerp3(v0->p, v1->p, t);
	v->n = lerp3(v0->n, v1->n, t);
	v->c = lerp3(v0->c, v1->c, t);
	v->uv = lerp2(v0->uv, v1->uv, t);
	v->tangent = lerp3(v0->tangent, v1->tangent, t);
	v->mtl = v0->mtl != nil? v0->mtl: v1->mtl;
	for(i = 0; i < v0->nattrs; i++){
		va.id = v0->attrs[i].id;
		va.type = v0->attrs[i].type;
		if(va.type == VAPoint)
			va.p = lerp3(v0->attrs[i].p, v1->attrs[i].p, t);
		else
			va.n = flerp(v0->attrs[i].n, v1->attrs[i].n, t);
		addvattr(v, &va);
	}
}

/*
 * barycentric attribute interpolation
 */
void
_berpvertex(BVertex *v, BVertex *v0, BVertex *v1, BVertex *v2, Point3 bc)
{
	Vertexattr va;
	int i;

	v->p = berp3(v0->p, v1->p, v2->p, bc);
	v->n = berp3(v0->n, v1->n, v2->n, bc);
	v->c = berp3(v0->c, v1->c, v2->c, bc);
	v->uv = berp2(v0->uv, v1->uv, v2->uv, bc);
	v->tangent = berp3(v0->tangent, v1->tangent, v2->tangent, bc);
	v->mtl = v0->mtl != nil? v0->mtl: v1->mtl != nil? v1->mtl: v2->mtl;
	for(i = 0; i < v0->nattrs; i++){
		va.id = v0->attrs[i].id;
		va.type = v0->attrs[i].type;
		if(va.type == VAPoint)
			va.p = berp3(v0->attrs[i].p, v1->attrs[i].p, v2->attrs[i].p, bc);
		else
			va.n = fberp(v0->attrs[i].n, v1->attrs[i].n, v2->attrs[i].n, bc);
		addvattr(v, &va);
	}
}

void
_addvertex(BVertex *a, BVertex *b)
{
	Vertexattr *va, *vb;

	a->n = addpt3(a->n, b->n);
	a->c = addpt3(a->c, b->c);
	a->uv = addpt2(a->uv, b->uv);
	a->tangent = addpt3(a->tangent, b->tangent);
	for(va = a->attrs; va < a->attrs + a->nattrs; va++){
		vb = b->attrs + (va - a->attrs);
		if(va->type == VAPoint)
			va->p = addpt3(va->p, vb->p);
		else
			va->n += vb->n;
	}
}

void
_mulvertex(BVertex *v, double s)
{
	Vertexattr *va;

	v->n = mulpt3(v->n, s);
	v->c = mulpt3(v->c, s);
	v->uv = mulpt2(v->uv, s);
	v->tangent = mulpt3(v->tangent, s);
	for(va = v->attrs; va < v->attrs + v->nattrs; va++){
		if(va->type == VAPoint)
			va->p = mulpt3(va->p, s);
		else
			va->n *= s;
	}
}

void
_addvattr(Vertexattrs *v, char *id, int type, void *val)
{
	Vertexattr va;

	va.id = id;
	va.type = type;
	switch(type){
	case VAPoint: va.p = *(Point3*)val; break;
	case VANumber: va.n = *(double*)val; break;
	default: sysfatal("unknown vertex attribute type '%d'", type);
	}
	addvattr(v, &va);
}

Vertexattr *
_getvattr(Vertexattrs *v, char *id)
{
	int i;

	for(i = 0; i < v->nattrs; i++)
		if(id != nil && strcmp(v->attrs[i].id, id) == 0)
			return &v->attrs[i];
	return nil;
}

void
_delvattrs(BVertex *v)
{
	free(v->attrs);
	v->attrs= nil;
	v->nattrs = 0;
}

void
_fprintvattrs(int fd, BVertex *v)
{
	int i;

	for(i = 0; i < v->nattrs; i++)
		fprint(fd, "id %s type %d v %g\n",
			v->attrs[i].id, v->attrs[i].type, v->attrs[i].n);
}
