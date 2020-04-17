#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include <graphics.h>

//static Memimage*
//imagetomemimage(Image *src)
//{
//	Memimage *dst;
//	uchar *buf;
//	uint buflen;
//
//	buflen = Dx(src->r)*Dy(src->r);
//	buflen *= (chantodepth(src->chan)+7)/8;
//	buf = malloc(buflen);
//	if(buf == nil)
//		sysfatal("malloc: %r");
//	dst = allocmemimage(src->r, src->chan);
//	if(dst == nil)
//		sysfatal("allocmemimage: %r");
//	if(src->repl){
//		dst->flags |= Frepl;
//		dst->clipr = Rect(-1e6, -1e6, 1e6, 1e6);
//	}
//	unloadimage(src, src->r, buf, buflen);
//	loadmemimage(dst, src->r, buf, buflen);
//	free(buf);
//	return dst;
//}

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

	cotan = 1/tan(fov/2*DEG);
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
	p0 = WORLD2NDC(c, p0);
	p1 = WORLD2NDC(c, p1);
	if(isclipping(p0) || isclipping(p1))
		return;
	line(c->viewport, toviewport(c, p0), toviewport(c, p1), end0, end1, 0, src, ZP);
}

Point
string3(Camera *c, Point3 p, Image *src, Font *f, char *s)
{
	p = WORLD2NDC(c, p);
	if(isclipping(p))
		return Pt(-1,-1);
	return string(c->viewport, toviewport(c, p), src, ZP, f, s);
}
