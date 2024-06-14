#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

Rectangle UR = {0,0,1,1};

static ulong
col2ul(Color c)
{
	uchar cbuf[4];

	cbuf[0] = fclamp(c.a, 0, 1)*0xFF;
	cbuf[1] = fclamp(c.b, 0, 1)*0xFF;
	cbuf[2] = fclamp(c.g, 0, 1)*0xFF;
	cbuf[3] = fclamp(c.r, 0, 1)*0xFF;
	return cbuf[3]<<24 | cbuf[2]<<16 | cbuf[1]<<8 | cbuf[0];
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

static void
pixel(Framebuf *fb, Point p, Color c)
{
	Color dc;
	ulong *dst;

	dst = fb->cb;
	dc = ul2col(dst[Dx(fb->r)*p.y + p.x]);
	c = lerp3(dc, c, c.a);	/* SoverD */
	dst[Dx(fb->r)*p.y + p.x] = col2ul(c);
}

static void
pixeln(Framebuf *fb, Point p, Color c)
{
	ulong *dst;

	dst = fb->nb;
	dst[Dx(fb->r)*p.y + p.x] = col2ul(c);
}

static int
isvisible(Point3 p)
{
	if(p.x < -p.w || p.x > p.w ||
	   p.y < -p.w || p.y > p.w ||
	   p.z < -p.w || p.z > p.w)
		return 0;
	return 1;
}

static int
isfacingback(Primitive p)
{
	double sa;	/* signed area */

	sa = p.v[0].p.x * p.v[1].p.y - p.v[0].p.y * p.v[1].p.x +
	     p.v[1].p.x * p.v[2].p.y - p.v[1].p.y * p.v[2].p.x +
	     p.v[2].p.x * p.v[0].p.y - p.v[2].p.y * p.v[0].p.x;
	return sa <= 0;
}

static Point3
_barycoords(Triangle2 t, Point2 p)
{
	Point2 p0p1 = subpt2(t.p1, t.p0);
	Point2 p0p2 = subpt2(t.p2, t.p0);
	Point2 pp0  = subpt2(t.p0, p);

	Point3 v = crossvec3(Vec3(p0p2.x, p0p1.x, pp0.x), Vec3(p0p2.y, p0p1.y, pp0.y));

	/* handle degenerate triangles—i.e. the ones where every point lies on the same line */
	if(fabs(v.z) < 1e-5)
		return Pt3(-1,-1,-1,1);
	return Pt3(1 - (v.x + v.y)/v.z, v.y/v.z, v.x/v.z, 1);
}

static void
rasterize(Rastertask *task)
{
	SUparams *params;
	Primitive prim;
	FSparams fsp;
	Triangle2 t;
	Rectangle bbox;
	Point p, dp, Δp, p0, p1;
	Point3 bc;
	Color c;
	double z, dplen, perc;
	int steep = 0, Δe, e, Δy;

	params = task->params;
	prim = task->p;
	memmove(prim.v, task->p.v, sizeof prim.v);
	fsp.su = params;
	memset(&fsp.v, 0, sizeof fsp.v);

	switch(prim.type){
	case PPoint:
		p = Pt(prim.v[0].p.x, prim.v[0].p.y);

		z = fclamp(prim.v[0].p.z, 0, 1);
		if(z <= params->fb->zb[p.x + p.y*Dx(params->fb->r)])
			break;
		params->fb->zb[p.x + p.y*Dx(params->fb->r)] = z;

		fsp.v = dupvertex(&prim.v[0]);
		fsp.p = p;
		c = params->fshader(&fsp);
		pixel(params->fb, p, c);
		delvattrs(&fsp.v);
		break;
	case PLine:
		p0 = Pt(prim.v[0].p.x, prim.v[0].p.y);
		p1 = Pt(prim.v[1].p.x, prim.v[1].p.y);
		/* clip it against our wr */
		if(rectclipline(task->wr, &p0, &p1) < 0)
			break;

		/* transpose the points */
		if(abs(p0.x-p1.x) < abs(p0.y-p1.y)){
			steep = 1;
			swapi(&p0.x, &p0.y);
			swapi(&p1.x, &p1.y);
		}

		/* make them left-to-right */
		if(p0.x > p1.x){
			swapi(&p0.x, &p1.x);
			swapi(&p0.y, &p1.y);
		}

		dp = subpt(p1, p0);
		Δe = 2*abs(dp.y);
		e = 0;
		Δy = p1.y > p0.y? 1: -1;

		for(p = p0; p.x <= p1.x; p.x++){
			Δp = subpt(p, p0);
			dplen = hypot(dp.x, dp.y);
			perc = dplen == 0? 0: hypot(Δp.x, Δp.y)/dplen;

			if(steep) swapi(&p.x, &p.y);

			z = flerp(prim.v[0].p.z, prim.v[1].p.z, perc);
			/* TODO get rid of the bounds check and make sure the clipping doesn't overflow */
			if(!ptinrect(p, params->fb->r) || z <= params->fb->zb[p.x + p.y*Dx(params->fb->r)])
				goto discard;
			params->fb->zb[p.x + p.y*Dx(params->fb->r)] = z;

			/* interpolate z⁻¹ and get actual z */
			z = flerp(prim.v[0].p.w, prim.v[1].p.w, perc);
			z = 1.0/(z < 1e-5? 1e-5: z);

			/* perspective-correct attribute interpolation  */
			perc *= prim.v[0].p.w * z;
			lerpvertex(&fsp.v, &prim.v[0], &prim.v[1], perc);

			fsp.p = p;
			c = params->fshader(&fsp);
			pixel(params->fb, p, c);
			delvattrs(&fsp.v);
discard:
			if(steep) swapi(&p.x, &p.y);

			e += Δe;
			if(e > dp.x){
				p.y += Δy;
				e -= 2*dp.x;
			}
		}
		break;
	case PTriangle:
		t.p0 = Pt2(prim.v[0].p.x, prim.v[0].p.y, 1);
		t.p1 = Pt2(prim.v[1].p.x, prim.v[1].p.y, 1);
		t.p2 = Pt2(prim.v[2].p.x, prim.v[2].p.y, 1);
		/* find the triangle's bbox and clip it against our wr */
		bbox.min.x = min(min(t.p0.x, t.p1.x), t.p2.x);
		bbox.min.y = min(min(t.p0.y, t.p1.y), t.p2.y);
		bbox.max.x = max(max(t.p0.x, t.p1.x), t.p2.x)+1;
		bbox.max.y = max(max(t.p0.y, t.p1.y), t.p2.y)+1;
		bbox.min.x = max(bbox.min.x, task->wr.min.x);
		bbox.min.y = max(bbox.min.y, task->wr.min.y);
		bbox.max.x = min(bbox.max.x, task->wr.max.x);
		bbox.max.y = min(bbox.max.y, task->wr.max.y);

		for(p.y = bbox.min.y; p.y < bbox.max.y; p.y++)
			for(p.x = bbox.min.x; p.x < bbox.max.x; p.x++){
				bc = _barycoords(t, Pt2(p.x,p.y,1));
				if(bc.x < 0 || bc.y < 0 || bc.z < 0)
					continue;

				z = fberp(prim.v[0].p.z, prim.v[1].p.z, prim.v[2].p.z, bc);
				if(z <= params->fb->zb[p.x + p.y*Dx(params->fb->r)])
					continue;
				params->fb->zb[p.x + p.y*Dx(params->fb->r)] = z;

				/* interpolate z⁻¹ and get actual z */
				z = fberp(prim.v[0].p.w, prim.v[1].p.w, prim.v[2].p.w, bc);
				z = 1.0/(z < 1e-5? 1e-5: z);

				/* perspective-correct attribute interpolation  */
				bc = modulapt3(bc, Vec3(prim.v[0].p.w*z,prim.v[1].p.w*z,prim.v[2].p.w*z));
				berpvertex(&fsp.v, &prim.v[0], &prim.v[1], &prim.v[2], bc);

				fsp.p = p;
				c = params->fshader(&fsp);
				pixel(params->fb, p, c);
				fsp.v.n.w = 1;
				pixeln(params->fb, p, fsp.v.n);
				delvattrs(&fsp.v);
			}
		break;
	}
}

static void
rasterizer(void *arg)
{
	Rasterparam *rp;
	Rastertask *task;
	SUparams *params;
	uvlong t0;
	int i;

	rp = arg;

	threadsetname("rasterizer %d", rp->id);

	while((task = recvp(rp->taskc)) != nil){
		t0 = nanosec();

		params = task->params;
		/* end of job */
		if(params->entity == nil){
			if(decref(params->job) < 1){
				nbsend(params->job->donec, nil);
				free(params);
			}
			free(task);
			continue;
		}

		if(params->job->times.Rn.t0 == 0)
			params->job->times.Rn.t0 = t0;

		rasterize(task);

		for(i = 0; i < task->p.type+1; i++)
			delvattrs(&task->p.v[i]);
		params->job->times.Rn.t1 = nanosec();
		free(params);
		free(task);
	}
}

static void
tilerdurden(void *arg)
{
	Tilerparam *tp;
	SUparams *params, *newparams;
	Rastertask *task;
	VSparams vsp;
	Primitive *ep, *p;			/* primitives to raster */
	Rectangle *wr, bbox;
	Channel **taskchans;
	ulong Δy, nproc;
	int i, np;
	uvlong t0;

	tp = arg;
	p = emalloc(sizeof(*p)*16);
	taskchans = tp->taskchans;
	nproc = tp->nproc;
	wr = emalloc(nproc*sizeof(Rectangle));

	threadsetname("tilerdurden %d", tp->id);

	while((params = recvp(tp->paramsc)) != nil){
		t0 = nanosec();
		if(params->job->times.Tn.t0 == 0)
			params->job->times.Tn.t0 = t0;

		/* end of job */
		if(params->entity == nil){
			if(decref(params->job) < 1){
				params->job->ref = nproc;
				for(i = 0; i < nproc; i++){
					task = emalloc(sizeof *task);
					memset(task, 0, sizeof *task);
					task->params = params;
					sendp(taskchans[i], task);
				}
			}
			continue;
		}
		vsp.su = params;

		wr[0] = params->fb->r;
		Δy = Dy(wr[0])/nproc;
		wr[0].max.y = wr[0].min.y + Δy;
		for(i = 1; i < nproc; i++)
			wr[i] = rectaddpt(wr[i-1], Pt(0,Δy));
		if(wr[nproc-1].max.y < params->fb->r.max.y)
			wr[nproc-1].max.y = params->fb->r.max.y;

		for(ep = params->eb; ep != params->ee; ep++){
			np = 1;	/* start with one. after clipping it might change */

			memmove(p, ep, sizeof *p);
			switch(ep->type){
			case PPoint:
				p[0].v[0].c = Pt3(1,1,1,1);
				p[0].v[0].mtl = ep->mtl;
				p[0].v[0].attrs = nil;
				p[0].v[0].nattrs = 0;

				vsp.v = &p[0].v[0];
				vsp.idx = 0;
				p[0].v[0].p = params->vshader(&vsp);

				if(!isvisible(p[0].v[0].p))
					break;

				p[0].v[0].p = clip2ndc(p[0].v[0].p);
				p[0].v[0].p = ndc2viewport(params->fb, p[0].v[0].p);

				bbox.min.x = p[0].v[0].p.x;
				bbox.min.y = p[0].v[0].p.y;
				bbox.max.x = p[0].v[0].p.x+1;
				bbox.max.y = p[0].v[0].p.y+1;

				for(i = 0; i < nproc; i++)
					if(rectXrect(bbox,wr[i])){
						newparams = emalloc(sizeof *newparams);
						*newparams = *params;
						task = emalloc(sizeof *task);
						task->params = newparams;
						task->wr = wr[i];
						memmove(&task->p, &p[0], sizeof task->p);
						task->p.v[0] = dupvertex(&p[0].v[0]);
						sendp(taskchans[i], task);
					}
				delvattrs(&p[0].v[0]);
				break;
			case PLine:
				for(i = 0; i < 2; i++){
					p[0].v[i].c = Pt3(1,1,1,1);
					p[0].v[i].mtl = ep->mtl;
					p[0].v[i].attrs = nil;
					p[0].v[i].nattrs = 0;

					vsp.v = &p[0].v[i];
					vsp.idx = i;
					p[0].v[i].p = params->vshader(&vsp);
				}

				if(!isvisible(p[0].v[0].p) || !isvisible(p[0].v[1].p))
					np = clipprimitive(p);

				while(np--){
					p[np].v[0].p = clip2ndc(p[np].v[0].p);
					p[np].v[1].p = clip2ndc(p[np].v[1].p);

					p[np].v[0].p = ndc2viewport(params->fb, p[np].v[0].p);
					p[np].v[1].p = ndc2viewport(params->fb, p[np].v[1].p);

					bbox.min.x = min(p[np].v[0].p.x, p[np].v[1].p.x);
					bbox.min.y = min(p[np].v[0].p.y, p[np].v[1].p.y);
					bbox.max.x = max(p[np].v[0].p.x, p[np].v[1].p.x)+1;
					bbox.max.y = max(p[np].v[0].p.y, p[np].v[1].p.y)+1;

					for(i = 0; i < nproc; i++)
						if(rectXrect(bbox,wr[i])){
							newparams = emalloc(sizeof *newparams);
							*newparams = *params;
							task = emalloc(sizeof *task);
							task->params = newparams;
							task->wr = wr[i];
							memmove(&task->p, &p[np], sizeof task->p);
							task->p.v[0] = dupvertex(&p[np].v[0]);
							task->p.v[1] = dupvertex(&p[np].v[1]);
							sendp(taskchans[i], task);
						}
					delvattrs(&p[np].v[0]);
					delvattrs(&p[np].v[1]);
				}
				break;
			case PTriangle:
				for(i = 0; i < 3; i++){
					p[0].v[i].c = Pt3(1,1,1,1);
					p[0].v[i].mtl = p->mtl;
					p[0].v[i].attrs = nil;
					p[0].v[i].nattrs = 0;
					p[0].v[i].tangent = p->tangent;

					vsp.v = &p[0].v[i];
					vsp.idx = i;
					p[0].v[i].p = params->vshader(&vsp);
				}

				if(!isvisible(p[0].v[0].p) || !isvisible(p[0].v[1].p) || !isvisible(p[0].v[2].p))
					np = clipprimitive(p);

				while(np--){
					p[np].v[0].p = clip2ndc(p[np].v[0].p);
					p[np].v[1].p = clip2ndc(p[np].v[1].p);
					p[np].v[2].p = clip2ndc(p[np].v[2].p);

					/* culling */
//					if(isfacingback(p[np]))
//						goto skiptri;

					p[np].v[0].p = ndc2viewport(params->fb, p[np].v[0].p);
					p[np].v[1].p = ndc2viewport(params->fb, p[np].v[1].p);
					p[np].v[2].p = ndc2viewport(params->fb, p[np].v[2].p);

					bbox.min.x = min(min(p[np].v[0].p.x, p[np].v[1].p.x), p[np].v[2].p.x);
					bbox.min.y = min(min(p[np].v[0].p.y, p[np].v[1].p.y), p[np].v[2].p.y);
					bbox.max.x = max(max(p[np].v[0].p.x, p[np].v[1].p.x), p[np].v[2].p.x)+1;
					bbox.max.y = max(max(p[np].v[0].p.y, p[np].v[1].p.y), p[np].v[2].p.y)+1;

					for(i = 0; i < nproc; i++)
						if(rectXrect(bbox,wr[i])){
							newparams = emalloc(sizeof *newparams);
							*newparams = *params;
							task = emalloc(sizeof *task);
							task->params = newparams;
							task->wr = wr[i];
							memmove(&task->p, &p[np], sizeof task->p);
							task->p.v[0] = dupvertex(&p[np].v[0]);
							task->p.v[1] = dupvertex(&p[np].v[1]);
							task->p.v[2] = dupvertex(&p[np].v[2]);
							sendp(taskchans[i], task);
						}
//skiptri:
					delvattrs(&p[np].v[0]);
					delvattrs(&p[np].v[1]);
					delvattrs(&p[np].v[2]);
				}
				break;
			}
		}
		params->job->times.Tn.t1 = nanosec();
		free(params);
	}
}

static void
entityproc(void *arg)
{
	Channel *paramsin, **paramsout, **taskchans;
	Tilerparam *tp;
	Rasterparam *rp;
	SUparams *params, *newparams;
	Primitive *eb, *ee;
	char *nprocs;
	ulong stride, nprims, nproc, nworkers;
	int i;
	uvlong t0;

	threadsetname("entityproc");

	paramsin = arg;
	nprocs = getenv("NPROC");
	if(nprocs == nil || (nproc = strtoul(nprocs, nil, 10)) < 2)
		nproc = 1;
	else
		nproc /= 2;
	free(nprocs);

	paramsout = emalloc(nproc*sizeof(*paramsout));
	taskchans = emalloc(nproc*sizeof(*taskchans));
	for(i = 0; i < nproc; i++){
		paramsout[i] = chancreate(sizeof(SUparams*), 8);
		tp = emalloc(sizeof *tp);
		tp->id = i;
		tp->paramsc = paramsout[i];
		tp->taskchans = taskchans;
		tp->nproc = nproc;
		proccreate(tilerdurden, tp, mainstacksize);
	}
	for(i = 0; i < nproc; i++){
		rp = emalloc(sizeof *rp);
		rp->id = i;
		rp->taskc = taskchans[i] = chancreate(sizeof(Rastertask*), 32);
		proccreate(rasterizer, rp, mainstacksize);
	}

	while((params = recvp(paramsin)) != nil){
		t0 = nanosec();
		if(params->job->times.E.t0 == 0)
			params->job->times.E.t0 = t0;

		/* end of job */
		if(params->entity == nil){
			params->job->ref = nproc;
			for(i = 0; i < nproc; i++)
				sendp(paramsout[i], params);
			continue;
		}

		eb = params->entity->mdl->prims;
		nprims = params->entity->mdl->nprims;
		ee = eb + nprims;

		if(nprims <= nproc){
			nworkers = nprims;
			stride = 1;
		}else{
			nworkers = nproc;
			stride = nprims/nproc;
		}

		for(i = 0; i < nworkers; i++){
			newparams = emalloc(sizeof *newparams);
			*newparams = *params;
			newparams->eb = eb + i*stride;
			newparams->ee = i == nworkers-1? ee: newparams->eb + stride;
			sendp(paramsout[i], newparams);
		}
		params->job->times.E.t1 = nanosec();
		free(params);
	}
}

static void
renderer(void *arg)
{
	Channel *jobc;
	Renderjob *job;
	Scene *sc;
	Entity *ent;
	SUparams *params;
	Channel *paramsc;
	uvlong time, lastid;

	threadsetname("renderer");

	jobc = arg;
	lastid = 0;
	paramsc = chancreate(sizeof(SUparams*), 8);

	proccreate(entityproc, paramsc, mainstacksize);

	while((job = recvp(jobc)) != nil){
		time = nanosec();
		job->times.R.t0 = time;
		job->id = lastid++;
		sc = job->scene;
		if(sc->nents < 1){
			nbsend(job->donec, nil);
			continue;
		}

		for(ent = sc->ents.next; ent != &sc->ents; ent = ent->next){
			params = emalloc(sizeof *params);
			memset(params, 0, sizeof *params);
			params->fb = job->fb;
			params->job = job;
			params->camera = job->camera;
			params->entity = ent;
			params->uni_time = time;
			params->vshader = job->shaders->vshader;
			params->fshader = job->shaders->fshader;
			sendp(paramsc, params);
		}
		/* mark end of job */
		params = emalloc(sizeof *params);
		memset(params, 0, sizeof *params);
		params->job = job;
		sendp(paramsc, params);

		job->times.R.t1 = nanosec();
	}
}

Renderer *
initgraphics(void)
{
	Renderer *r;

	r = emalloc(sizeof *r);
	r->c = chancreate(sizeof(Renderjob*), 8);
	proccreate(renderer, r->c, mainstacksize);
	return r;
}
