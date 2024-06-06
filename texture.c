#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

enum {
	CUBEMAP_FACE_LEFT,	/* -x */
	CUBEMAP_FACE_RIGHT,	/* +x */
	CUBEMAP_FACE_BOTTOM,	/* -y */
	CUBEMAP_FACE_TOP,	/* +y */
	CUBEMAP_FACE_FRONT,	/* -z */
	CUBEMAP_FACE_BACK,	/* +z */
};

/*
 * uv-coords belong to the 1st quadrant (v grows bottom-up),
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
_memreadcolor(Memimage *i, Point sp)
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

/*
 * nearest-neighbour sampler
 */
Color
neartexsampler(Memimage *i, Point2 uv)
{
	return _memreadcolor(i, uv2tp(uv, i));
}

/*
 * bilinear sampler
 */
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
	c1 = lerp3(_memreadcolor(i, r.min), _memreadcolor(i, Pt(r.max.x, r.min.y)), 0.5);
	c2 = lerp3(_memreadcolor(i, Pt(r.min.x, r.max.y)), _memreadcolor(i, r.max), 0.5);
	return lerp3(c1, c2, 0.5);
}

Color
texture(Memimage *i, Point2 uv, Color(*sampler)(Memimage*,Point2))
{
	return sampler(i, uv);
}

/* cubemap sampling */

Cubemap *
readcubemap(char *paths[6])
{
	Cubemap *cm;
	char **p;
	int fd;

	cm = emalloc(sizeof *cm);
	memset(cm, 0, sizeof *cm);
	
	for(p = paths; p < paths+6; p++){
		assert(*p != nil);
		fd = open(*p, OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		cm->faces[p-paths] = readmemimage(fd);
		if(cm->faces[p-paths] == nil)
			sysfatal("readmemimage: %r");
		close(fd);
	}
	return cm;
}

void
freecubemap(Cubemap *cm)
{
	int i;

	for(i = 0; i < 6; i++)
		freememimage(cm->faces[i]);
	free(cm->name);
	free(cm);
}

/*
 * references:
 * 	- https://github.com/zauonlok/renderer/blob/9ed5082f0eda453f0b2a0d5ec37cf5a60f0207f6/renderer/core/texture.c#L206
 * 	- “Cubemap Texture Selection”, OpenGL ES 2.0 § 3.7.5, November 2010
 */
Color
cubemaptexture(Cubemap *cm, Point3 d, Color(*sampler)(Memimage*,Point2))
{
	Point2 uv;
	double ax, ay, az, ma, sc, tc;
	int face;

	ax = fabs(d.x);
	ay = fabs(d.y);
	az = fabs(d.z);

	if(ax > ay && ax > az){
		ma = ax;
		if(d.x > 0){
			face = CUBEMAP_FACE_RIGHT;
			sc = -d.z;
			tc = -d.y;
		}else{
			face = CUBEMAP_FACE_LEFT;
			sc =  d.z;
			tc = -d.y;
		}
	}else if(ay > az){
		ma = ay;
		if(d.y > 0){
			face = CUBEMAP_FACE_TOP;
			sc = d.x;
			tc = d.z;
		}else{
			face = CUBEMAP_FACE_BOTTOM;
			sc =  d.x;
			tc = -d.z;
		}
	}else{
		ma = az;
		if(d.z > 0){
			face = CUBEMAP_FACE_BACK;
			sc =  d.x;
			tc = -d.y;
		}else{
			face = CUBEMAP_FACE_FRONT;
			sc = -d.x;
			tc = -d.y;
		}
	}

	uv.x = (sc/ma + 1)/2;
	uv.y = 1 - (tc/ma + 1)/2;
	uv.w = 1;
	return sampler(cm->faces[face], uv);
}
