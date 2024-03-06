#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

/*
 * uv-coords belong to the 4th quadrant (v grows bottom-up),
 * hence the need to reverse the v coord.
 */
static Point
uv2tp(Point2 uv, Memimage *i)
{
	assert(uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1);
	return Pt(uv.x*Dx(i->r), (1 - uv.y)*Dy(i->r));
}

static Color
ul2col(ulong l)
{
	Color c;

	c.a = (l     & 0xff)/255.0;
	c.b = (l>>8  & 0xff)/255.0;
	c.g = (l>>16 & 0xff)/255.0;
	c.r = (l>>24 & 0xff)/255.0;
	return c;
}

static Color
cbuf2col(uchar b[4])
{
	Color c;

	c.a = b[0] / 255.0;
	c.b = b[1] / 255.0;
	c.g = b[2] / 255.0;
	c.r = b[3] / 255.0;
	return c;
}

static Color
_memreadpixel(Memimage *i, Point sp)
{
	uchar cbuf[4];

	switch(i->chan){
	case RGB24:
		unloadmemimage(i, rectaddpt(UR, sp), cbuf+1, sizeof cbuf - 1);
		cbuf[0] = 0xFF;
		break;
	case RGBA32:
		unloadmemimage(i, rectaddpt(UR, sp), cbuf, sizeof cbuf);
		break;
	case XRGB32:
		unloadmemimage(i, rectaddpt(UR, sp), cbuf, sizeof cbuf);
		memmove(cbuf+1, cbuf, 3);
		cbuf[0] = 0xFF;
		break;
	}

	return cbuf2col(cbuf);
}

Color
neartexsampler(Memimage *i, Point2 uv)
{
	return _memreadpixel(i, uv2tp(uv, i));
}

Color
bilitexsampler(Memimage *i, Point2 uv)
{
	Rectangle r;
	Color c1, c2;

	r = rectaddpt(UR, uv2tp(uv, i));
	if(r.min.x < i->r.min.x){
		r.min.x++;
		r.max.x++;
	}if(r.min.y < i->r.min.y){
		r.min.y++;
		r.max.y++;
	}if(r.max.x >= i->r.max.x){
		r.min.x--;
		r.max.x--;
	}if(r.max.y >= i->r.max.y){
		r.min.y--;
		r.max.y--;
	}
	c1 = lerp3(_memreadpixel(i, r.min), _memreadpixel(i, Pt(r.max.x, r.min.y)), 0.5);
	c2 = lerp3(_memreadpixel(i, Pt(r.min.x, r.max.y)), _memreadpixel(i, r.max), 0.5);
	return lerp3(c1, c2, 0.5);
}

Color
texture(Memimage *i, Point2 uv, Color(*sampler)(Memimage*,Point2))
{
	return sampler(i, uv);
}
