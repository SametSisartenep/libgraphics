#include <u.h>
#include <libc.h>
#include <draw.h>
#include <geometry.h>
#include <graphics.h>

static Point2
flatten(Camera *c, Point3 p)
{
	Point2 p2;
	Matrix S = {
		Dx(c->viewport->r)/2, 0, 0,
		0, Dy(c->viewport->r)/2, 0,
		0, 0, 1,
	}, T = {
		1, 0, 1,
		0, 1, 1,
		0, 0, 1,
	};

	p2 = Pt2(p.x, p.y, p.w);
	if(p2.w != 0)
		p2 = divpt2(p2, p2.w);
	mulm(S, T);
	p2 = xform(p2, S);
	return p2;
}

Point3
world2vcs(Camera *c, Point3 p)
{
	return rframexform3(p, *c);
}

Point3
vcs2ndc(Camera *c, Point3 p)
{
	return xform3(p, c->proj);
}

Point3
world2ndc(Camera *c, Point3 p)
{
	return vcs2ndc(c, world2vcs(c, p));
}

/* requires p to be in NDC */
int
isclipping(Point3 p)
{
	if(p.x > p.w || p.x < -p.w ||
	   p.y > p.w || p.y < -p.w ||
	   p.z > p.w || p.z < 0)
		return 1;
	return 0;
}

/* Liang-Barsky algorithm, requires p0, p1 to be in NDC */
int
clipline3(Point3 *p0, Point3 *p1)
{
	Point3 q0, q1, v;
	int m0, m1, i;
	double ti, to, th;
	double c0[3*2] = {
		p0->w + p0->x, p0->w - p0->x, p0->w + p0->y,
		p0->w - p0->y,         p0->z, p0->w - p0->z,
	}, c1[3*2] = {
		p1->w + p1->x, p1->w - p1->x, p1->w + p1->y,
		p1->w - p1->y,         p1->z, p1->w - p1->z,
	};

	/* bit-encoded regions */
	m0 = (c0[0] < 0) << 0 |
	     (c0[1] < 0) << 1 |
	     (c0[2] < 0) << 2 |
	     (c0[3] < 0) << 3 |
	     (c0[4] < 0) << 4 |
	     (c0[5] < 0) << 5;
	m1 = (c1[0] < 0) << 0 |
	     (c1[1] < 0) << 1 |
	     (c1[2] < 0) << 2 |
	     (c1[3] < 0) << 3 |
	     (c1[4] < 0) << 4 |
	     (c1[5] < 0) << 5;

	if((m0 & m1) != 0)
		return 1;	/* trivially rejected */
	if((m0 | m1) == 0)
		return 0;	/* trivially accepted */

	ti = 0;
	to = 1;
	for(i = 0; i < 3*2; i++){
		if(c1[i] < 0){
			th = c0[i] / (c0[i]-c1[i]);
			if(th < to)
				to = th;
		}else if(c0[i] < 0){
			th = c0[i] / (c0[i]-c1[i]);
			if(th < ti)
				ti = th;
		}
		if(ti > to)
			return 1;
	}

	/* chop line to fit inside NDC */
	q0 = *p0;
	q1 = *p1;
	v = subpt3(q1, q0);
	if(m0 != 0)
		*p0 = addpt3(q0, mulpt3(v, ti));
	if(m1 != 0)
		*p1 = addpt3(q0, mulpt3(v, to));

	return 0;
}

Point
toviewport(Camera *c, Point3 p)
{
	Point2 p2;
	RFrame rf = {
		c->viewport->r.min.x, c->viewport->r.max.y, 1,
		1, 0, 0,
		0, -1, 0
	};

	p2 = invrframexform(flatten(c, p), rf);
	return Pt(p2.x, p2.y);
}

Point2
fromviewport(Camera *c, Point p)
{
	RFrame rf = {
		c->viewport->r.min.x, c->viewport->r.max.y, 1,
		1, 0, 0,
		0, -1, 0
	};

	return rframexform(Pt2(p.x,p.y,1), rf);
}

void
perspective(Matrix3 m, double fov, double a, double n, double f)
{
	double cotan;

	cotan = 1/tan(fov/2);
	identity3(m);
	m[0][0] =  cotan/a;
	m[1][1] =  cotan;
	m[2][2] = -(f+n)/(f-n);
	m[2][3] = -2*f*n/(f-n);
	m[3][2] = -1;
}

void
orthographic(Matrix3 m, double l, double r, double b, double t, double n, double f)
{
	identity3(m);
	m[0][0] =  2/(r - l);
	m[1][1] =  2/(t - b);
	m[2][2] = -2/(f - n);
	m[0][3] = -(r + l)/(r - l);
	m[1][3] = -(t + b)/(t - b);
	m[2][3] = -(f + n)/(f - n);
}

void
line3(Camera *c, Point3 p0, Point3 p1, int end0, int end1, Image *src)
{
	p0 = world2ndc(c, p0);
	p1 = world2ndc(c, p1);
	if(clipline3(&p0, &p1))
		return;
	line(c->viewport, toviewport(c, p0), toviewport(c, p1), end0, end1, 0, src, ZP);
}

Point
string3(Camera *c, Point3 p, Image *src, Font *f, char *s)
{
	p = world2ndc(c, p);
	if(isclipping(p))
		return Pt(-1,-1);
	return string(c->viewport, toviewport(c, p), src, ZP, f, s);
}
