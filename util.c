#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

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
memsetf(void *dp, float v, usize len)
{
	float *p, *ep;

	for(p = dp, ep = p+len; p < ep; p++)
		*p = v;
}

void
memsetl(void *dp, ulong v, usize len)
{
	ulong *p, *ep;

	for(p = dp, ep = p+len; p < ep; p++)
		*p = v;
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

Memimage *
dupmemimage(Memimage *i)
{
	Memimage *ni;

	if(i == nil)
		return nil;

	ni = allocmemimaged(i->r, i->chan, i->data);
	if(ni == nil)
		sysfatal("allocmemimaged: %r");
	ni->data->ref++;
	return ni;
}
