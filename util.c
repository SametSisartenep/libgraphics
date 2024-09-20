#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

Point
minpt(Point a, Point b)
{
	return (Point){
		min(a.x, b.x),
		min(a.y, b.y)
	};
}

Point
maxpt(Point a, Point b)
{
	return (Point){
		max(a.x, b.x),
		max(a.y, b.y)
	};
}

Point2
modulapt2(Point2 a, Point2 b)
{
	return (Point2){a.x*b.x, a.y*b.y, a.w*b.w};
}

Point2
minpt2(Point2 a, Point2 b)
{
	return (Point2){
		min(a.x, b.x),
		min(a.y, b.y),
		min(a.w, b.w)
	};
}

Point2
maxpt2(Point2 a, Point2 b)
{
	return (Point2){
		max(a.x, b.x),
		max(a.y, b.y),
		max(a.w, b.w)
	};
}

Point3
modulapt3(Point3 a, Point3 b)
{
	return (Point3){a.x*b.x, a.y*b.y, a.z*b.z, a.w*b.w};
}

Point3
minpt3(Point3 a, Point3 b)
{
	return (Point3){
		min(a.x, b.x),
		min(a.y, b.y),
		min(a.z, b.z),
		min(a.w, b.w)
	};
}

Point3
maxpt3(Point3 a, Point3 b)
{
	return (Point3){
		max(a.x, b.x),
		max(a.y, b.y),
		max(a.z, b.z),
		max(a.w, b.w)
	};
}

void
memsetf(void *dp, float v, usize len)
{
	float *p;

	p = dp;
	while(len--)
		*p++ = v;
}

void
memsetl(void *dp, ulong v, usize len)
{
	ulong *p;

	p = dp;
	while(len--)
		*p++ = v;
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
