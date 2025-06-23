#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
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
	double px, py, pz, pw;

	px = p.x; py = p.y; pz = p.z; pw = p.w;
	r[0] = m[0][0]*px + m[0][1]*py + m[0][2]*pz + m[0][3]*pw;
	r[1] = m[1][0]*px + m[1][1]*py + m[1][2]*pz + m[1][3]*pw;
	r[2] = m[2][0]*px + m[2][1]*py + m[2][2]*pz + m[2][3]*pw;
	r[3] = m[3][0]*px + m[3][1]*py + m[3][2]*pz + m[3][3]*pw;
	r[4] = m[4][0]*px + m[4][1]*py + m[4][2]*pz + m[4][3]*pw;
	r[5] = m[5][0]*px + m[5][1]*py + m[5][2]*pz + m[5][3]*pw;
}

static int
addvert(Polygon *p, Vertex v)
{
	if(++p->n > p->cap)
		p->v = _erealloc(p->v, (p->cap = p->n)*sizeof(*p->v));
	p->v[p->n-1] = v;
	return p->n;
}

static void
cleanpoly(Polygon *p)
{
	int i;

	for(i = 0; i < p->n; i++)
		_delvattrs(&p->v[i]);
	p->n = 0;
}

static void
fprintpoly(int fd, Polygon *p)
{
	int i;

	for(i = 0; i < p->n; i++)
		fprint(fd, "%d/%lud p %V\n", i, p->n, p->v[i].p);
}

/*
 * references:
 * 	- “Clipping Using Homogeneous Coordinates”, James F. Blinn, Martin E. Newell, SIGGRAPH '78, pp. 245-251
 * 	- https://cs418.cs.illinois.edu/website/text/clipping.html
 * 	- https://github.com/aap/librw/blob/14dab85dcae6f3762fb2b1eda4d58d8e67541330/tools/playground/tl_tests.cpp#L522
 */
int
_clipprimitive(Primitive *p, Primitive *cp)
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
	Polygon Vinp, Voutp, *Vin, *Vout;
	Vertex *v0, *v1, v;	/* edge verts and new vertex (line-plane intersection) */
	int i, j, np;

	np = 0;
	Vin = &Vinp;
	Vout = &Voutp;
	memset(Vin, 0, sizeof Vinp);
	memset(Vout, 0, sizeof Voutp);
	for(i = 0; i < p->type+1; i++)
		addvert(Vin, p->v[i]);

	for(j = 0; j < 6 && Vin->n > 0; j++){
		for(i = 0; i < Vin->n; i++){
			v0 = &Vin->v[i];
			v1 = &Vin->v[(i+1) % Vin->n];

			mulsdm(sd0, sdm, v0->p);
			mulsdm(sd1, sdm, v1->p);

			if(sd0[j] < 0 && sd1[j] < 0)
				continue;

			if(sd0[j] >= 0 && sd1[j] >= 0)
				goto allin;

			d0 = (j&1) == 0? sd0[j]: -sd0[j];
			d1 = (j&1) == 0? sd1[j]: -sd1[j];
			perc = d0/(d0 - d1);

			memset(&v, 0, sizeof v);
			_lerpvertex(&v, v0, v1, perc);
			addvert(Vout, v);

			if(sd1[j] >= 0){
allin:
				addvert(Vout, _dupvertex(v1));
			}
		}
		cleanpoly(Vin);
		if(j < 6-1)
			SWAP(Polygon*, &Vin, &Vout);
	}

	if(Vout->n < 2)
		cleanpoly(Vout);
	else switch(p->type){
	case PLine:
		cp[0] = *p;
		cp[0].v[0] = _dupvertex(&Vout->v[0]);
		cp[0].v[1] = eqpt3(Vout->v[0].p, Vout->v[1].p)? _dupvertex(&Vout->v[2]): _dupvertex(&Vout->v[1]);
		cleanpoly(Vout);
		np = 1;
		break;
	case PTriangle:
		/* triangulate */
		for(i = 0; i < Vout->n-2; i++, np++){
			/*
			 * when performing fan triangulation, indices 0 and 2
			 * are referenced on every triangle, so duplicate them
			 * to avoid complications during rasterization.
			 */
			cp[np] = *p;
			cp[np].v[0] = i < Vout->n-2-1? _dupvertex(&Vout->v[0]): Vout->v[0];
			cp[np].v[1] = Vout->v[i+1];
			cp[np].v[2] = i < Vout->n-2-1? _dupvertex(&Vout->v[i+2]): Vout->v[i+2];
		}
		break;
	}
	free(Vout->v);
	free(Vin->v);

	return np;
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

/* lerp vertex attributes to match the new positions */
static void
adjustverts(Point *p0, Point *p1, Vertex *v0, Vertex *v1)
{
	Vertex v[2];
	Point3 dp;
	Point Δp;
	double len, perc;

	memset(v, 0, sizeof v);

	dp = subpt3(v1->p, v0->p);
	len = hypot(dp.x, dp.y);

	Δp = subpt((Point){v0->p.x, v0->p.y}, *p0);
	perc = len == 0? 0: hypot(Δp.x, Δp.y)/len;
	_lerpvertex(&v[0], v0, v1, perc);

	Δp = subpt((Point){v0->p.x, v0->p.y}, *p1);
	perc = len == 0? 0: hypot(Δp.x, Δp.y)/len;
	_lerpvertex(&v[1], v0, v1, perc);

	_delvattrs(v0);
	_delvattrs(v1);
	*v0 = v[0];
	*v1 = v[1];
}

/*
 * Cohen-Sutherland rectangle-line clipping
 */
int
_rectclipline(Rectangle r, Point *p0, Point *p1, Vertex *v0, Vertex *v1)
{
	int code0, code1;
	int Δx, Δy;
	double m;

	Δx = p1->x - p0->x;
	Δy = p1->y - p0->y;
	m = Δx == 0? 0: (double)Δy/Δx;

	for(;;){
		code0 = outcode(*p0, r);
		code1 = outcode(*p1, r);

		if(lineisinside(code0, code1)){
			adjustverts(p0, p1, v0, v1);
			return 0;
		}else if(lineisoutside(code0, code1))
			return -1;

		if(ptisinside(code0)){
			SWAP(Point, p0, p1);
			SWAP(int, &code0, &code1);
			SWAP(Vertex, v0, v1);
		}

		if(code0 & CLIPL){
			p0->y += (r.min.x - p0->x)*m;
			p0->x = r.min.x;
		}else if(code0 & CLIPR){
			p0->y += (r.max.x - p0->x)*m;
			p0->x = r.max.x;
		}else if(code0 & CLIPT){
			if(p0->x != p1->x && m != 0)
				p0->x += (r.min.y - p0->y)/m;
			p0->y = r.min.y;
		}else if(code0 & CLIPB){
			if(p0->x != p1->x && m != 0)
				p0->x += (r.max.y - p0->y)/m;
			p0->y = r.max.y;
		}
	}
}
