#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

int
min(int a, int b)
{
	return a < b? a: b;
}

int
max(int a, int b)
{
	return a > b? a: b;
}

double
fmin(double a, double b)
{
	return a < b? a: b;
}

double
fmax(double a, double b)
{
	return a > b? a: b;
}

void
swapi(int *a, int *b)
{
	int t;

	t = *a;
	*a = *b;
	*b = t;
}

void
swappt(Point *a, Point *b)
{
	Point t;

	t = *a;
	*a = *b;
	*b = t;
}

Point2
modulapt2(Point2 a, Point2 b)
{
	return (Point2){a.x*b.x, a.y*b.y, a.w*b.w};
}

Point3
modulapt3(Point3 a, Point3 b)
{
	return (Point3){a.x*b.x, a.y*b.y, a.z*b.z, a.w*b.w};
}

void
memsetd(double *p, double v, usize len)
{
	double *dp;

	for(dp = p; dp < p+len; dp++)
		*dp = v;
}

Memimage *
rgb(ulong c)
{
	Memimage *i;

	i = eallocmemimage(UR, RGBA32);
	i->flags |= Frepl;
	i->clipr = Rect(-1e6, -1e6, 1e6, 1e6);
	memfillcolor(i, c);
	return i;
}
