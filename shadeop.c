#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

double
sign(double n)
{
	return n == 0? 0: n < 0? -1: 1;
}

double
step(double edge, double n)
{
	if(n < edge)
		return 0;
	return 1;
}

double
smoothstep(double edge0, double edge1, double n)
{
	double t;

	t = fclamp((n-edge0)/(edge1-edge0), 0, 1);
	return t*t * (3 - 2*t);
}

/* see Equation 5.16, Real-Time Rendering 4th ed. § 5.2.2 */
static double
dfalloff(LightSource *l, double d)
{
	if(l->cutoff <= 0)
		return 0;

	d = d/l->cutoff;
	d *= d;
	d = max(0, 1 - d);
	return d*d;
}

Color
getlightcolor(LightSource *l, Point3 p, Point3 n)
{
	double cθs, cθu, cθp, t, r;
	Point3 ldir;
	Color c;

	ldir = subpt3(l->p, p);
	r = vec3len(ldir);
	ldir = divpt3(ldir, r);

	switch(l->type){
	case LightDirectional:
		t = max(0, dotvec3(mulpt3(l->dir, -1), n));
		c = mulpt3(l->c, t);
		break;
	case LightPoint:
		t = max(0, dotvec3(ldir, n));
		c = mulpt3(l->c, t);

		/* attenuation */
		c = mulpt3(c, dfalloff(l, r));
		break;
	case LightSpot:
		/* see “Spotlights”, Real-Time Rendering 4th ed. § 5.2.2 */
		cθs = dotvec3(mulpt3(ldir, -1), l->dir);
		cθu = cos(l->θu);
		cθp = cos(l->θp);

//		return mulpt3(l->c, smoothstep(cθu, cθp, cθs));
		t = fclamp((cθs - cθu)/(cθp - cθu), 0, 1);

		c = mulpt3(l->c, t*t);

		/* attenuation */
		c = mulpt3(c, dfalloff(l, r));
		break;
	default: sysfatal("alien light form detected");
	}
	return c;
}

Color
getscenecolor(Scene *s, Point3 p, Point3 n)
{
	LightSource *l;
	Color c;

	c = Vec3(0,0,0);
	for(l = s->lights.next; l != &s->lights; l = l->next)
		c = addpt3(c, getlightcolor(l, p, n));
	return c;
}
