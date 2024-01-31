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

	i = eallocmemimage(UR, screen->chan);
	i->flags |= Frepl;
	i->clipr = Rect(-1e6, -1e6, 1e6, 1e6);
	memfillcolor(i, c);
	return i;
}
