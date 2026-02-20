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
	Vertexattr *vp, *ve;

	assert(va->id != nil);

	vp = v->attrs;
	ve = vp + v->nattrs;
	for(; vp < ve; vp++)
		if(strcmp(vp->id, va->id) == 0){
			*vp = *va;
			return;
		}
	if(v->nattrs == MAXVATTRS)
		sysfatal("too many vertex attributes or uniforms");
	v->attrs[v->nattrs++] = *va;
}

/*
 * linear attribute interpolation
 */
void
_lerpvertex(BVertex *v, BVertex *v0, BVertex *v1, double t)
{
	Vertexattr va, *v0a, *v1a, *ve;

	v->p = lerp3(v0->p, v1->p, t);
	v->n = lerp3(v0->n, v1->n, t);
	v->c = lerp3(v0->c, v1->c, t);
	v->uv = lerp2(v0->uv, v1->uv, t);
	v->tangent = lerp3(v0->tangent, v1->tangent, t);
	v->mtl = v0->mtl != nil? v0->mtl: v1->mtl;
	v->nattrs = 0;
	v0a = v0->attrs;
	v1a = v1->attrs;
	ve = v0a + v0->nattrs;
	for(; v0a < ve; v0a++, v1a++){
		va.id = v0a->id;
		va.type = v0a->type;
		if(va.type == VAPoint)
			va.p = lerp3(v0a->p, v1a->p, t);
		else
			va.n = flerp(v0a->n, v1a->n, t);
		addvattr(v, &va);
	}
}

/*
 * barycentric attribute interpolation
 */
void
_berpvertex(BVertex *v, BVertex *v0, BVertex *v1, BVertex *v2, Point3 bc)
{
	Vertexattr va, *v0a, *v1a, *v2a, *ve;

	v->p = berp3(v0->p, v1->p, v2->p, bc);
	v->n = berp3(v0->n, v1->n, v2->n, bc);
	v->c = berp3(v0->c, v1->c, v2->c, bc);
	v->uv = berp2(v0->uv, v1->uv, v2->uv, bc);
	v->tangent = berp3(v0->tangent, v1->tangent, v2->tangent, bc);
	v->mtl = v0->mtl != nil? v0->mtl: v1->mtl != nil? v1->mtl: v2->mtl;
	v->nattrs = 0;
	v0a = v0->attrs;
	v1a = v1->attrs;
	v2a = v2->attrs;
	ve = v0a + v0->nattrs;
	for(; v0a < ve; v0a++, v1a++, v2a++){
		va.id = v0a->id;
		va.type = v0a->type;
		if(va.type == VAPoint)
			va.p = berp3(v0a->p, v1a->p, v2a->p, bc);
		else
			va.n = fberp(v0a->n, v1a->n, v2a->n, bc);
		addvattr(v, &va);
	}
}

void
_addvertex(BVertex *a, BVertex *b)
{
	Vertexattr *va, *vb, *ve;

	a->n = addpt3(a->n, b->n);
	a->c = addpt3(a->c, b->c);
	a->uv = addpt2(a->uv, b->uv);
	a->tangent = addpt3(a->tangent, b->tangent);
	ve = a->attrs + a->nattrs;
	for(va = a->attrs, vb = b->attrs; va < ve; va++, vb++){
		if(va->type == VAPoint)
			va->p = addpt3(va->p, vb->p);
		else
			va->n += vb->n;
	}
}

void
_mulvertex(BVertex *v, double s)
{
	Vertexattr *va, *ve;

	v->n = mulpt3(v->n, s);
	v->c = mulpt3(v->c, s);
	v->uv = mulpt2(v->uv, s);
	v->tangent = mulpt3(v->tangent, s);
	ve = v->attrs + v->nattrs;
	for(va = v->attrs; va < ve; va++){
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
	Vertexattr *va, *ve;

	if(id == nil)
		return nil;

	ve = v->attrs + v->nattrs;
	for(va = v->attrs; va < ve; va++)
		if(strcmp(va->id, id) == 0)
			return va;
	return nil;
}

void
_fprintvattrs(int fd, Vertexattrs *v)
{
	static char *idtype[] = {
	 [VAPoint]	"point",
	 [VANumber]	"number",
	};
	Vertexattr *va;

	for(va = v->attrs; va < v->attrs + v->nattrs; va++){
		fprint(fd, "id %s type %s", va->id, idtype[va->type]);
		switch(va->type){
		case VAPoint:
			fprint(fd, " %V\n", va->p);
			break;
		case VANumber:
			fprint(fd, " %g\n", va->n);
			break;
		}
	}
}
