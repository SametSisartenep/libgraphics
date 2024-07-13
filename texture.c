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
uv2tp(Point2 uv, Texture *t)
{
	assert(uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1);
	return Pt(uv.x*Dx(t->image->r), (1 - uv.y)*Dy(t->image->r));
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
_memreadcolor(Texture *t, Point sp)
{
	Color c;
	uchar cbuf[4];

	switch(t->image->chan){
	case RGB24:
		unloadmemimage(t->image, rectaddpt(UR, sp), cbuf+1, sizeof cbuf - 1);
		cbuf[0] = 0xFF;
		break;
	case RGBA32:
		unloadmemimage(t->image, rectaddpt(UR, sp), cbuf, sizeof cbuf);
		break;
	case XRGB32:
		unloadmemimage(t->image, rectaddpt(UR, sp), cbuf, sizeof cbuf);
		memmove(cbuf+1, cbuf, 3);
		cbuf[0] = 0xFF;
		break;
	}

	c = cbuf2col(cbuf);
	switch(t->type){
	case sRGBTexture: c = srgb2linear(c); break;
	}
	return c;
}

/*
 * nearest-neighbour sampler
 */
Color
neartexsampler(Texture *t, Point2 uv)
{
	return _memreadcolor(t, uv2tp(uv, t));
}

/*
 * bilinear sampler
 */
Color
bilitexsampler(Texture *t, Point2 uv)
{
	Rectangle r;
	Color c1, c2;

	r = rectaddpt(UR, uv2tp(uv, t));
	if(r.min.x < t->image->r.min.x){
		r.min.x++;
		r.max.x++;
	}if(r.min.y < t->image->r.min.y){
		r.min.y++;
		r.max.y++;
	}if(r.max.x >= t->image->r.max.x){
		r.min.x--;
		r.max.x--;
	}if(r.max.y >= t->image->r.max.y){
		r.min.y--;
		r.max.y--;
	}
	c1 = lerp3(_memreadcolor(t, r.min), _memreadcolor(t, Pt(r.max.x, r.min.y)), 0.5);
	c2 = lerp3(_memreadcolor(t, Pt(r.min.x, r.max.y)), _memreadcolor(t, r.max), 0.5);
	return lerp3(c1, c2, 0.5);
}

Color
sampletexture(Texture *t, Point2 uv, Color(*sampler)(Texture*,Point2))
{
	return sampler(t, uv);
}

Texture *
alloctexture(int type, Memimage *i)
{
	Texture *t;

	t = emalloc(sizeof *t);
	t->image = i;
	t->type = type;
	return t;
}

Texture *
duptexture(Texture *t)
{
	Texture *nt;

	if(t == nil)
		return nil;

	nt = alloctexture(t->type, nil);
	nt->image = dupmemimage(t->image);
	return nt;
}

void
freetexture(Texture *t)
{
	if(t == nil)
		return;

	freememimage(t->image);
	free(t);
}

/* cubemap sampling */

Cubemap *
readcubemap(char *paths[6])
{
	Cubemap *cm;
	Memimage *i;
	char **p;
	int fd;

	cm = emalloc(sizeof *cm);
	memset(cm, 0, sizeof *cm);
	
	for(p = paths; p < paths+6; p++){
		assert(*p != nil);
		fd = open(*p, OREAD);
		if(fd < 0)
			sysfatal("open: %r");
		i = readmemimage(fd);
		if(i == nil)
			sysfatal("readmemimage: %r");
		cm->faces[p-paths] = alloctexture(sRGBTexture, i);
		close(fd);
	}
	return cm;
}

void
freecubemap(Cubemap *cm)
{
	int i;

	if(cm == nil)
		return;

	for(i = 0; i < 6; i++)
		freetexture(cm->faces[i]);
	free(cm->name);
	free(cm);
}

/*
 * references:
 * 	- https://github.com/zauonlok/renderer/blob/9ed5082f0eda453f0b2a0d5ec37cf5a60f0207f6/renderer/core/texture.c#L206
 * 	- “Cubemap Texture Selection”, OpenGL ES 2.0 § 3.7.5, November 2010
 */
Color
samplecubemap(Cubemap *cm, Point3 d, Color(*sampler)(Texture*,Point2))
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
