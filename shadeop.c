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

/* TODO apply attenuation for punctual lights */
Color
getlightcolor(LightSource *l, Point3 dir)
{
	double cθs, cθu, cθp, t;

	/* see “Spotlights”, Real-Time Rendering 4th ed. § 5.2.2 */
	if(l->type == LightSpot){
		cθs = dotvec3(mulpt3(dir, -1), l->dir);
		cθu = cos(l->θu);
		cθp = cos(l->θp);
//		return mulpt3(l->c, smoothstep(cθu, cθp, cθs));
		t = fclamp((cθs - cθu)/(cθp - cθu), 0, 1);
		return mulpt3(l->c, t*t);
	}
	return l->c;
}
