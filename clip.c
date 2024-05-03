#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

enum {
	CLIPL = 1,
	CLIPR = 2,
	CLIPT = 4,
	CLIPB = 8,
};

static void
mulsdm(double r[6], double m[6][4], Point3 p)
{
	int i;

	for(i = 0; i < 6; i++)
		r[i] = m[i][0]*p.x + m[i][1]*p.y + m[i][2]*p.z + m[i][3]*p.w;
}

static int
addvert(Polygon *p, Vertex v)
{
	if(++p->n > p->cap)
		p->v = erealloc(p->v, (p->cap = p->n)*sizeof(*p->v));
	p->v[p->n-1] = v;
	return p->n;
}

static void
swappoly(Polygon *a, Polygon *b)
{
	Polygon tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static void
cleanpoly(Polygon *p)
{
	int i;

	for(i = 0; i < p->n; i++)
		delvattrs(&p->v[i]);
	p->n = 0;
}

/*
 * references:
 * 	- James F. Blinn, Martin E. Newell, “Clipping Using Homogeneous Coordinates”,
 * 	  SIGGRAPH '78, pp. 245-251
 * 	- https://cs418.cs.illinois.edu/website/text/clipping.html
 * 	- https://github.com/aap/librw/blob/14dab85dcae6f3762fb2b1eda4d58d8e67541330/tools/playground/tl_tests.cpp#L522
 */
int
clipprimitive(Primitive *p)
{
	/* signed distance from each clipping plane */
	static double sdm[6][4] = {
		 1,  0,  0, 1,	/* l */
		-1,  0,  0, 1,	/* r */
		 0,  1,  0, 1,	/* b */
		 0, -1,  0, 1,	/* t */
		 0,  0,  1, 1,	/* f */
		 0,  0, -1, 1,	/* n */
	};
	double sd0[6], sd1[6];
	double d0, d1, perc;
	Polygon Vin, Vout;
	Vertex *v0, *v1, v;	/* edge verts and new vertex (line-plane intersection) */
	int i, j, nt;

	nt = 0;
	memset(&Vin, 0, sizeof Vin);
	memset(&Vout, 0, sizeof Vout);
	for(i = 0; i < p[0].type+1; i++)
		addvert(&Vin, p[0].v[i]);

	for(j = 0; j < 6 && Vin.n > 0; j++){
		for(i = 0; i < Vin.n; i++){
			v0 = &Vin.v[i];
			v1 = &Vin.v[(i+1) % Vin.n];

			mulsdm(sd0, sdm, v0->p);
			mulsdm(sd1, sdm, v1->p);

			if(sd0[j] < 0 && sd1[j] < 0)
				continue;

			if(sd0[j] >= 0 && sd1[j] >= 0)
				goto allin;

			d0 = (j&1) == 0? sd0[j]: -sd0[j];
			d1 = (j&1) == 0? sd1[j]: -sd1[j];
			perc = d0/(d0 - d1);

			lerpvertex(&v, v0, v1, perc);
			addvert(&Vout, v);

			if(sd1[j] >= 0){
allin:
				addvert(&Vout, dupvertex(v1));
			}
		}
		cleanpoly(&Vin);
		if(j < 6-1)
			swappoly(&Vin, &Vout);
	}

	if(Vout.n < 2)
		cleanpoly(&Vout);
	else switch(p[0].type){
	case PLine:
		/* TODO fix line clipping (they disappear instead, why?) */
		p[0].v[0] = dupvertex(&Vout.v[0]);
		p[0].v[1] = dupvertex(&Vout.v[1]);
		cleanpoly(&Vout);
		break;
	case PTriangle:
		/* triangulate */
		for(i = 0; i < Vout.n-2; i++, nt++){
			/*
			 * when performing fan triangulation, indices 0 and 2
			 * are referenced on every triangle, so duplicate them
			 * to avoid complications during rasterization.
			 */
			memmove(&p[nt], &p[0], sizeof *p);
			p[nt].v[0] = i < Vout.n-2-1? dupvertex(&Vout.v[0]): Vout.v[0];
			p[nt].v[1] = Vout.v[i+1];
			p[nt].v[2] = i < Vout.n-2-1? dupvertex(&Vout.v[i+2]): Vout.v[i+2];
		}
		break;
	}
	free(Vout.v);
	free(Vin.v);

	return nt;
}

static int
ptisinside(int code)
{
	return !code;
}

static int
lineisinside(int code0, int code1)
{
	return !(code0|code1);
}

static int
lineisoutside(int code0, int code1)
{
	return code0 & code1;
}

static int
outcode(Point p, Rectangle r)
{
	int code;

	code = 0;
	if(p.x < r.min.x) code |= CLIPL;
	if(p.x > r.max.x) code |= CLIPR;
	if(p.y < r.min.y) code |= CLIPT;
	if(p.y > r.max.y) code |= CLIPB;
	return code;
}

/*
 * Cohen-Sutherland rectangle-line clipping
 */
void
rectclipline(Rectangle r, Point *p0, Point *p1)
{
	int code0, code1;
	int Δx;
	double m;

	Δx = p1->x - p0->x;
	m = Δx == 0? 0: (p1->y - p0->y)/Δx;

	for(;;){
		code0 = outcode(*p0, r);
		code1 = outcode(*p1, r);

		if(lineisinside(code0, code1) || lineisoutside(code0, code1))
			break;

		if(ptisinside(code0)){
			swappt(p0, p1);
			swapi(&code0, &code1);
		}

		if(code0 & CLIPL){
			p0->y += (r.min.x - p0->x)*m;
			p0->x = r.min.x;
		}else if(code0 & CLIPR){
			p0->y += (r.max.x - p0->x)*m;
			p0->x = r.max.x;
		}else if(code0 & CLIPB){
			if(p0->x != p1->x)
				p0->x += (r.min.y - p0->y)/m;
			p0->y = r.min.y;
		}else if(code0 & CLIPT){
			if(p0->x != p1->x)
				p0->x += (r.max.y - p0->y)/m;
			p0->y = r.max.y;
		}
	}
}
