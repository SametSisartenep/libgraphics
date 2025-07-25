#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

/*
 * transforms p from e's reference frame into
 * the world.
 */
Point3
model2world(Entity *e, Point3 p)
{
	return invrframexform3(p, *e);
}

/*
 * transforms p from the world reference frame
 * to c's one (aka Viewing Coordinate System).
 */
Point3
world2vcs(Camera *c, Point3 p)
{
	return rframexform3(p, *c);
}

/*
 * projects p from the VCS to clip space, placing
 * p.[xyz] ∈ (-∞,-w)∪[-w,w]∪(w,∞) where [-w,w]
 * represents the visibility volume.
 *
 * the clipping planes are:
 *
 * 	|   -w   |   w   |
 *	+----------------+
 * 	| left   | right |
 * 	| bottom | top   |
 * 	| far    | near  |
 */
Point3
vcs2clip(Camera *c, Point3 p)
{
	return xform3(p, c->proj);
}

Point3
world2clip(Camera *c, Point3 p)
{
	return vcs2clip(c, world2vcs(c, p));
}

/*
 * performs the perspective division, placing
 * p.[xyz] ∈ [-1,1] and p.w = 1/z
 * (aka Normalized Device Coordinates).
 *
 * p.w is kept as z⁻¹ so we can later do
 * perspective-correct attribute interpolation.
 */
Point3
clip2ndc(Point3 p)
{
	double w;

	w = p.w == 0? 0: 1.0/p.w;
	return (Point3){p.x*w, p.y*w, p.z*w, w};
}

/*
 * scales p to fit the destination viewport,
 * placing p.x ∈ [0,width], p.y ∈ [0,height],
 * p.z ∈ [0,1] and leaving p.w intact.
 */
Point3
ndc2viewport(Framebuf *fb, Point3 p)
{
	Matrix3 view = {
		Dx(fb->r)/2.0,             0,       0,       Dx(fb->r)/(2*p.w),
		0,            -Dy(fb->r)/2.0,       0,       Dy(fb->r)/(2*p.w),
		0,                         0, 1.0/2.0,             1.0/(2*p.w),
		0,                         0,       0,                   1,
	};

	return xform3(p, view);
}

Point3
viewport2ndc(Framebuf *fb, Point3 p)
{
	p.x = 2*p.x/Dx(fb->r) - 1;
	p.y = 1 - 2*p.y/Dy(fb->r);
	p.z = 2*p.z - 1;
	p.w = 1;
	return p;
}

Point3
ndc2vcs(Camera *c, Point3 p)
{
	Point3 np;

	np = xform3(p, c->invproj);
	np.w = np.w == 0? 0: 1.0/np.w;
	np.x *= np.w;
	np.y *= np.w;
	np.z *= np.w;
	np.w = 1;
	return np;
}

Point3
viewport2vcs(Camera *c, Point3 p)
{
	return ndc2vcs(c, viewport2ndc(c->view->getfb(c->view), p));
}

Point3
vcs2world(Camera *c, Point3 p)
{
	return invrframexform3(p, *c);
}

Point3
viewport2world(Camera *c, Point3 p)
{
	return vcs2world(c, viewport2vcs(c, p));
}

Point3
world2model(Entity *e, Point3 p)
{
	return rframexform3(p, *e);
}

/*
 * adapted from the equations in https://www.songho.ca/opengl/gl_projectionmatrix.html#perspective
 */
void
perspective(Matrix3 m, double fovy, double a, double n, double f)
{
	double cotan;

	cotan = 1/tan(fovy/2);
	memset(m, 0, sizeof(Matrix3));
	m[0][0] =  cotan/a;
	m[1][1] =  cotan;
	m[2][2] =  (f+n)/(f-n);
	m[2][3] =  2*f*n/(f-n);
	m[3][2] = -1;
}

/*
 * adapted from the equations in https://www.songho.ca/opengl/gl_projectionmatrix.html#ortho
 */
void
orthographic(Matrix3 m, double l, double r, double b, double t, double n, double f)
{
	identity3(m);
	m[0][0] =  2/(r-l);
	m[1][1] =  2/(t-b);
	m[2][2] =  2/(f-n);
	m[0][3] = -(r+l)/(r-l);
	m[1][3] = -(t+b)/(t-b);
	m[2][3] =  (f+n)/(f-n);
}
