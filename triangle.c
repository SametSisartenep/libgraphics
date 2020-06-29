#include <u.h>
#include <libc.h>
#include <draw.h>
#include <geometry.h>
#include <graphics.h>

/*
 * comparison of a point p with an edge [e0 e1]
 * p to the right: +
 * p to the left: -
 * p on the edge: 0
 */
static int
edgeptcmp(Point e0, Point e1, Point p)
{
	Point3 e0p, e01, r;

	p = subpt(p, e0);
	e1 = subpt(e1, e0);
	e0p = Vec3(p.x,p.y,0);
	e01 = Vec3(e1.x,e1.y,0);
	r = crossvec3(e0p, e01);

	/* clamp to avoid overflow */
	return fclamp(r.z, -1, 1); /* e0.x*e1.y - e0.y*e1.x */
}

Triangle
Trian(int x0, int y0, int x1, int y1, int x2, int y2)
{
	return (Triangle){Pt(x0, y0), Pt(x1, y1), Pt(x2, y2)};
}

Triangle
Trianpt(Point p0, Point p1, Point p2)
{
	return (Triangle){p0, p1, p2};
};

void
triangle(Image *dst, Triangle t, int thick, Image *src, Point sp)
{
	Point pl[4];

	pl[0] = t.p0;
	pl[1] = t.p1;
	pl[2] = t.p2;
	pl[3] = pl[0];

	poly(dst, pl, nelem(pl), 0, 0, thick, src, sp);
}

void
filltriangle(Image *dst, Triangle t, Image *src, Point sp)
{
	Point pl[3];

	pl[0] = t.p0;
	pl[1] = t.p1;
	pl[2] = t.p2;

	fillpoly(dst, pl, nelem(pl), 0, src, sp);
}

int
ptintriangle(Point p, Triangle t)
{
	/* counter-clockwise check */
	return edgeptcmp(t.p0, t.p2, p) <= 0 &&
		edgeptcmp(t.p2, t.p1, p) <= 0 &&
		edgeptcmp(t.p1, t.p0, p) <= 0;
}
