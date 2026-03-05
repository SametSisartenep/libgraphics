#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

Raster *
_allocraster(char *name, Rectangle rr, ulong chan)
{
	Raster *r;

	if(chan > FLOAT32){
		werrstr("bad format");
		return nil;
	}

	r = _emalloc(sizeof(Raster) + 4*Dx(rr)*Dy(rr));
	memset(r, 0, sizeof(Raster));
	if(name != nil)
		snprint(r->name, sizeof r->name, "%s", name);
	r->chan = chan;
	r->r = rr;
	return r;
}

void
_clearraster(Raster *r, ulong v)
{
	_memsetl(r->data, v, Dx(r->r)*Dy(r->r));
}

void
_fclearraster(Raster *r, float v)
{
	_memsetl(r->data, *(ulong*)&v, Dx(r->r)*Dy(r->r));
}

uchar *
_rasterbyteaddr(Raster *r, Point p)
{
	return (uchar*)&r->data[p.y*Dx(r->r) + p.x];
}

void
_rasterput(Raster *r, Point p, void *v)
{
	*(u32int*)_rasterbyteaddr(r, p) = *(u32int*)v;
}

void
_rasterget(Raster *r, Point p, void *v)
{
	*(u32int*)v = *(u32int*)_rasterbyteaddr(r, p);
}

void
_rasterputcolor(Raster *r, Point p, ulong c)
{
	_rasterput(r, p, &c);
}

ulong
_rastergetcolor(Raster *r, Point p)
{
	ulong c;

	_rasterget(r, p, &c);
	return c;
}

void
_rasterputfloat(Raster *r, Point p, float v)
{
	_rasterput(r, p, &v);
}

float
_rastergetfloat(Raster *r, Point p)
{
	float v;

	_rasterget(r, p, &v);
	return v;
}

void
_freeraster(Raster *r)
{
	free(r);
}
