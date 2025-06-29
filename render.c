#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

static Vertexattr *
sparams_getuniform(Shaderparams *sp, char *id)
{
	return _getvattr(sp->su->stab, id);
}

void
setuniform(Shadertab *st, char *id, int type, void *val)
{
	_addvattr(st, id, type, val);
}

static Vertexattr *
sparams_getattr(Shaderparams *sp, char *id)
{
	return _getvattr(sp->v, id);
}

static void
sparams_setattr(Shaderparams *sp, char *id, int type, void *val)
{
	_addvattr(sp->v, id, type, val);
}

static void
sparams_toraster(Shaderparams *sp, char *rname, void *v)
{
	Framebuf *fb;
	Raster *r;
	ulong c;

	/* keep the user away from the color buffer */
	if(rname == nil || v == nil)
		return;

	fb = sp->su->fb;
	r = fb->fetchraster(fb, rname);
	if(r == nil)
		return;

	switch(r->chan){
	case COLOR32:
		c = col2ul(*(Color*)v);
		_rasterput(r, sp->p, &c);
		break;
	case FLOAT32:
		_rasterput(r, sp->p, v);
		break;
	}
}

static void
pixel(Raster *fb, Point p, Color c, int blend)
{
	Color dc;

	if(blend){
		dc = srgb2linear(ul2col(getpixel(fb, p)));
		c = lerp3(dc, c, c.a);	/* SoverD */
//		c = addpt3(mulpt3(dc, 1), mulpt3(c, 1-c.a));
//		c = subpt3(Vec3(1,1,1), subpt3(dc, c));
//		c = subpt3(addpt3(dc, c), Vec3(1,1,1));
	}
	putpixel(fb, p, col2ul(linear2srgb(c)));
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
isfacingback(BPrimitive *p)
{
	double sa;	/* signed double area */

	sa = p->v[0].p.x * p->v[1].p.y - p->v[0].p.y * p->v[1].p.x +
	     p->v[1].p.x * p->v[2].p.y - p->v[1].p.y * p->v[2].p.x +
	     p->v[2].p.x * p->v[0].p.y - p->v[2].p.y * p->v[0].p.x;
	return sa <= 0;	/* 0 - CCW, 1 - CW */
}

static int
istoporleft(Point2 *e0, Point2 *e1)
{
	Point2 e01;

	e01 = subpt2(*e1, *e0);
	return e01.y > 0			/* left */
		|| (e01.y == 0 && e01.x < 0);	/* top */
}

static void
initAbuf(Framebuf *fb)
{
	if(fb->abuf.stk != nil)
		return;

	fb->abuf.stk = _emalloc(Dx(fb->r)*Dy(fb->r)*sizeof(Astk));
	memset(fb->abuf.stk, 0, Dx(fb->r)*Dy(fb->r)*sizeof(Astk));
}

static void
pushtoAbuf(Framebuf *fb, Point p, Color c, float z)
{
	Abuf *buf;
	Astk *stk;
	int i;

	buf = &fb->abuf;
	stk = &buf->stk[p.y*Dx(fb->r) + p.x];
	if(stk->size % 16 == 0){
		stk->items = _erealloc(stk->items, (stk->size + 16)*sizeof(*stk->items));
		memset(&stk->items[stk->size], 0, 16*sizeof(*stk->items));
	}

	/* TODO don't push pixels that are behind opaque fragments */
	for(i = 0; i < stk->size; i++)
		if(z > stk->items[i].z)
			break;

	if(i < stk->size)
		memmove(&stk->items[i+1], &stk->items[i], (stk->size - i)*sizeof(*stk->items));
	stk->items[i] = (Fragment){c, z};

	if(stk->size++ < 1){
		stk->p = p;
		qlock(buf);
		if(buf->nact % 64 == 0)
			buf->act = _erealloc(buf->act, (buf->nact + 64)*sizeof(*buf->act));
		buf->act[buf->nact++] = stk;
		qunlock(buf);
	}
}

static void
squashAbuf(Framebuf *fb, Rectangle *wr, int blend)
{
	Abuf *buf;
	Astk *stk;
	Raster *cr, *zr;
	int x, y, ss;

	buf = &fb->abuf;
	cr = fb->rasters;
	zr = cr->next;
	for(y = wr->min.y; y < wr->max.y; y++)
	for(x = wr->min.x; x < wr->max.x; x++){
		stk = &buf->stk[y*Dx(*wr) + x];
		ss = stk->size;
		if(ss < 1)
			continue;
		while(ss--)
			pixel(cr, stk->p, stk->items[ss].c, blend);
		/* write to the depth buffer as well */
		putdepth(zr, stk->p, stk->items[0].z);
	}
}

static void
rasterizept(Rastertask *task)
{
	Raster *cr, *zr;
	BPrimitive *prim;
	Point p;
	Color c;
	float z;
	uint ropts;

	prim = &task->p;

	cr = task->fb->rasters;
	zr = cr->next;

	ropts = task->camera->rendopts;

	p = (Point){prim->v[0].p.x, prim->v[0].p.y};

	z = fclamp(prim->v[0].p.z, 0, 1);
	if((ropts & RODepth) && z <= getdepth(zr, p))
		return;

	*task->fsp->v = prim->v[0];
	task->fsp->p = p;
	c = task->stab->fs(task->fsp);
	if(c.a == 0)			/* discard non-colors */
		return;
	if(ropts & RODepth)
		putdepth(zr, p, z);
	if(ropts & ROAbuff)
		pushtoAbuf(task->fb, p, c, z);
	else
		pixel(cr, p, c, ropts & ROBlend);

	if(task->clipr->min.x < 0){
		task->clipr->min = p;
		task->clipr->max = addpt(p, (Point){1,1});
	}else{
		task->clipr->min = minpt(task->clipr->min, p);
		task->clipr->max = maxpt(task->clipr->max, addpt(p, (Point){1,1}));
	}
}

static void
rasterizeline(Rastertask *task)
{
	Raster *cr, *zr;
	BPrimitive *prim;
	Point p, dp, Δp, p0, p1;
	Color c;
	double dplen, perc;
	float z, pcz;
	uint ropts;
	int steep, Δe, e, Δy;

	prim = &task->p;

	cr = task->fb->rasters;
	zr = cr->next;

	ropts = task->camera->rendopts;

	p0 = (Point){prim->v[0].p.x, prim->v[0].p.y};
	p1 = (Point){prim->v[1].p.x, prim->v[1].p.y};
	/* clip it against our wr */
	if(_rectclipline(task->wr, &p0, &p1, prim->v+0, prim->v+1) < 0)
		return;

	steep = 0;
	/* transpose the points */
	if(abs(p0.x-p1.x) < abs(p0.y-p1.y)){
		steep = 1;
		SWAP(int, &p0.x, &p0.y);
		SWAP(int, &p1.x, &p1.y);
	}

	/* make them left-to-right */
	if(p0.x > p1.x){
		SWAP(Point, &p0, &p1);
		SWAP(BVertex, prim->v+0, prim->v+1);
	}

	dp = subpt(p1, p0);
	Δe = 2*abs(dp.y);
	e = 0;
	Δy = p1.y > p0.y? 1: -1;

	for(p = p0; p.x <= p1.x; p.x++){
		Δp = subpt(p, p0);
		dplen = hypot(dp.x, dp.y);
		perc = dplen == 0? 0: hypot(Δp.x, Δp.y)/dplen;

		if(steep) SWAP(int, &p.x, &p.y);

		z = flerp(prim->v[0].p.z, prim->v[1].p.z, perc);
		/* TODO get rid of the bounds check and make sure the clipping doesn't overflow */
		if(!ptinrect(p, task->fb->r) ||
		   ((ropts & RODepth) && z <= getdepth(zr, p)))
			goto discard;

		/* interpolate z⁻¹ and get actual z */
		pcz = flerp(prim->v[0].p.w, prim->v[1].p.w, perc);
		pcz = 1.0/(pcz < ε1? ε1: pcz);

		/* perspective-correct attribute interpolation  */
		perc *= prim->v[0].p.w * pcz;
		_lerpvertex(task->fsp->v, prim->v+0, prim->v+1, perc);

		task->fsp->p = p;
		c = task->stab->fs(task->fsp);
		if(c.a == 0)			/* discard non-colors */
			goto discard;
		if(ropts & RODepth)
			putdepth(zr, p, z);
		if(ropts & ROAbuff)
			pushtoAbuf(task->fb, p, c, z);
		else
			pixel(cr, p, c, ropts & ROBlend);

		if(task->clipr->min.x < 0){
			task->clipr->min = p;
			task->clipr->max = addpt(p, (Point){1,1});
		}else{
			task->clipr->min = minpt(task->clipr->min, p);
			task->clipr->max = maxpt(task->clipr->max, addpt(p, (Point){1,1}));
		}
discard:
		if(steep) SWAP(int, &p.x, &p.y);

		e += Δe;
		if(e > dp.x){
			p.y += Δy;
			e -= 2*dp.x;
		}
	}
}

static Point3
_barycoords(Triangle2 t, Point2 p)
{
	Point2 p0p1 = subpt2(t.p1, t.p0);
	Point2 p0p2 = subpt2(t.p2, t.p0);
	Point2 pp0  = subpt2(t.p0, p);

	Point3 v = crossvec3(Vec3(p0p2.x, p0p1.x, pp0.x), Vec3(p0p2.y, p0p1.y, pp0.y));

	/* handle degenerate triangles—i.e. the ones where every point lies on the same line */
	if(fabs(v.z) < ε1)
		return Pt3(-1,-1,-1,1);
	/* barycoords and inverse signed double area (for the gradients) */
	return mulpt3((Point3){v.z - v.x - v.y, v.y, v.x, 1}, 1/v.z);
}

static void
rasterizetri(Rastertask *task)
{
	Raster *cr, *zr;
	BPrimitive *prim;
	pGradient ∇bc;
//	vGradient ∇v;
//	fGradient ∇z, ∇pcz;
//	BVertex v, *vp;
	Triangle2 t;
	Point p;
	Point3 bc;
	Color c;
	float z, pcz;
	uint ropts;

	prim = &task->p;

	cr = task->fb->rasters;
	zr = cr->next;

	ropts = task->camera->rendopts;

//	memset(&v, 0, sizeof v);
//	vp = &v;

	t.p0 = (Point2){prim->v[0].p.x, prim->v[0].p.y, 1};
	t.p1 = (Point2){prim->v[1].p.x, prim->v[1].p.y, 1};
	t.p2 = (Point2){prim->v[2].p.x, prim->v[2].p.y, 1};

	∇bc.p0 = _barycoords(t, (Point2){task->wr.min.x+0.5, task->wr.min.y+0.5, 1});
	∇bc.dx = mulpt3((Point3){t.p2.y - t.p1.y, t.p0.y - t.p2.y, t.p1.y - t.p0.y, 0}, ∇bc.p0.w);
	∇bc.dy = mulpt3((Point3){t.p1.x - t.p2.x, t.p2.x - t.p0.x, t.p0.x - t.p1.x, 0}, ∇bc.p0.w);

//	/* TODO find a good method to apply the fill rule */
//	if(istoporleft(&t.p1, &t.p2)){
//		∇bc.p0.x -= ∇bc.p0.w;
//		∇bc.dx.x -= ∇bc.p0.w;
//		∇bc.dy.x -= ∇bc.p0.w;
//	}
//	if(istoporleft(&t.p2, &t.p0)){
//		∇bc.p0.y -= ∇bc.p0.w;
//		∇bc.dx.y -= ∇bc.p0.w;
//		∇bc.dy.y -= ∇bc.p0.w;
//	}
//	if(istoporleft(&t.p0, &t.p1)){
//		∇bc.p0.z -= ∇bc.p0.w;
//		∇bc.dx.z -= ∇bc.p0.w;
//		∇bc.dy.z -= ∇bc.p0.w;
//	}

	/* perspective divide vertex attributes */
	_mulvertex(prim->v+0, prim->v[0].p.w);
	_mulvertex(prim->v+1, prim->v[1].p.w);
	_mulvertex(prim->v+2, prim->v[2].p.w);

//	memset(&∇v, 0, sizeof ∇v);
//	_berpvertex(&∇v.v0, prim->v+0, prim->v+1, prim->v+2, ∇bc.p0);
//	_berpvertex(&∇v.dx, prim->v+0, prim->v+1, prim->v+2, ∇bc.dx);
//	_berpvertex(&∇v.dy, prim->v+0, prim->v+1, prim->v+2, ∇bc.dy);
//
//	∇z.f0 = fberp(prim->v[0].p.z, prim->v[1].p.z, prim->v[2].p.z, ∇bc.p0);
//	∇z.dx = fberp(prim->v[0].p.z, prim->v[1].p.z, prim->v[2].p.z, ∇bc.dx);
//	∇z.dy = fberp(prim->v[0].p.z, prim->v[1].p.z, prim->v[2].p.z, ∇bc.dy);
//
//	∇pcz.f0 = fberp(prim->v[0].p.w, prim->v[1].p.w, prim->v[2].p.w, ∇bc.p0);
//	∇pcz.dx = fberp(prim->v[0].p.w, prim->v[1].p.w, prim->v[2].p.w, ∇bc.dx);
//	∇pcz.dy = fberp(prim->v[0].p.w, prim->v[1].p.w, prim->v[2].p.w, ∇bc.dy);

	for(p.y = task->wr.min.y; p.y < task->wr.max.y; p.y++){
		bc = ∇bc.p0;
//		*task->fsp->v = ∇v.v0;
//		z = ∇z.f0;
//		pcz = ∇pcz.f0;
	for(p.x = task->wr.min.x; p.x < task->wr.max.x; p.x++){
		if(bc.x < 0 || bc.y < 0 || bc.z < 0)
			goto discard;

		z = fberp(prim->v[0].p.z, prim->v[1].p.z, prim->v[2].p.z, bc);
		if((ropts & RODepth) && z <= getdepth(zr, p))
			goto discard;

		/* interpolate z⁻¹ and get actual z */
		pcz = fberp(prim->v[0].p.w, prim->v[1].p.w, prim->v[2].p.w, bc);
		pcz = 1.0/(pcz < ε1? ε1: pcz);

		/* perspective-correct attribute interpolation  */
		_berpvertex(task->fsp->v, prim->v+0, prim->v+1, prim->v+2, mulpt3(bc, pcz));

//		_loadvertex(vp, task->fsp->v);
//		_mulvertex(vp, 1/(pcz < ε1? ε1: pcz));

//		SWAP(BVertex*, &vp, &task->fsp->v);
		task->fsp->p = p;
		c = task->stab->fs(task->fsp);
//		SWAP(BVertex*, &vp, &task->fsp->v);
		if(c.a == 0)			/* discard non-colors */
			goto discard;
		if(ropts & RODepth)
			putdepth(zr, p, z);
		if(ropts & ROAbuff)
			pushtoAbuf(task->fb, p, c, z);
		else
			pixel(cr, p, c, ropts & ROBlend);

		if(task->clipr->min.x < 0){
			task->clipr->min = p;
			task->clipr->max = addpt(p, (Point){1,1});
		}else{
			task->clipr->min = minpt(task->clipr->min, p);
			task->clipr->max = maxpt(task->clipr->max, addpt(p, (Point){1,1}));
		}
discard:
		bc = addpt3(bc, ∇bc.dx);
//		_addvertex(task->fsp->v, &∇v.dx);
//		z += ∇z.dx;
//		pcz += ∇pcz.dx;
	}
		∇bc.p0 = addpt3(∇bc.p0, ∇bc.dy);
//		_addvertex(&∇v.v0, &∇v.dy);
//		∇z.f0 += ∇z.dy;
//		∇pcz.f0 += ∇pcz.dy;
	}

//	_delvattrs(vp);
//	_delvattrs(&∇v.dx);
//	_delvattrs(&∇v.dy);
}

static void
rasterizer(void *arg)
{
	static void(*rasterfn[])(Rastertask*) = {
	 [PPoint]	rasterizept,
	 [PLine]	rasterizeline,
	 [PTriangle]	rasterizetri,
	};
	Rasterparam *rp;
	Rastertask *task;
	Renderjob *job;
	BVertex v;
	Shaderparams fsp;
	int i;

	rp = arg;
	threadsetname("rasterizer %d", rp->id);

	memset(&fsp, 0, sizeof fsp);
	memset(&v, 0, sizeof v);
	fsp.v = &v;
	fsp.getuniform = sparams_getuniform;
	fsp.getattr = sparams_getattr;
	fsp.setattr = nil;
	fsp.toraster = sparams_toraster;

	while((task = recvp(rp->taskc)) != nil){
		job = task->job;
		if(job->rctl->doprof && job->times.Rn[rp->id].t0 == 0)
			job->times.Rn[rp->id].t0 = nanosec();

		if(task->op == OP_END){
			if(job->camera->rendopts & ROAbuff)
				squashAbuf(job->fb, &task->wr, job->camera->rendopts & ROBlend);

			if(decref(job) < 1){
				/* set the clipr to the union of bboxes from the rasterizers */
				for(i = 1; i < job->ncliprects; i++){
					if(job->cliprects[i].min.x < 0)
						continue;
					job->cliprects[0].min = job->cliprects[0].min.x < 0?
						job->cliprects[i].min:
						minpt(job->cliprects[0].min, job->cliprects[i].min);
					job->cliprects[0].max = job->cliprects[0].max.x < 0?
						job->cliprects[i].max:
						maxpt(job->cliprects[0].max, job->cliprects[i].max);
				}
				job->fb->clipr = job->cliprects[0];

				if(job->rctl->doprof)
					job->times.Rn[rp->id].t1 = nanosec();

				nbsend(job->donec, nil);
			}else if(job->rctl->doprof)
				job->times.Rn[rp->id].t1 = nanosec();
			free(task);
			continue;
		}

		fsp.su = &task->SUparams;
		task->fsp = &fsp;
		(*rasterfn[task->p.type])(task);

		_delvattrs(&v);
		if(task->p.type != PPoint)
			for(i = 0; i < task->p.type+1; i++)
				_delvattrs(&task->p.v[i]);
		free(task);
	}
}

static void
initworkrects(Rectangle *wr, int nwr, Rectangle *fbr)
{
	int i, Δy;

	wr[0] = *fbr;
	Δy = Dy(wr[0])/nwr;
	wr[0].max.y = wr[0].min.y + Δy;
	for(i = 1; i < nwr; i++)
		wr[i] = rectaddpt(wr[i-1], Pt(0,Δy));
	if(wr[nwr-1].max.y < fbr->max.y)
		wr[nwr-1].max.y = fbr->max.y;
}

static BPrimitive *
assembleprim(BPrimitive *d, Primitive *s, Model *m)
{
	Vertex *v;
	int i;

	d->type = s->type;
	for(i = 0; i < s->type+1; i++){
		v = itemarrayget(m->verts, s->v[i]);
		d->v[i].p = *(Point3*)itemarrayget(m->positions, v->p);
		d->v[i].n = v->n == NaI? ZP3: *(Point3*)itemarrayget(m->normals, v->n);
		d->v[i].uv = v->uv == NaI? ZP2: *(Point2*)itemarrayget(m->texcoords, v->uv);
		d->v[i].c = v->c == NaI? ZP3: *(Color*)itemarrayget(m->colors, v->c);
	}
	d->tangent = s->tangent == NaI? ZP3: *(Point3*)itemarrayget(m->tangents, s->tangent);
	d->mtl = s->mtl;
	return d;
}

static void
tiler(void *arg)
{
	Tilerparam *tp;
	SUparams *params;
	Rastertask *task;
	Shaderparams vsp;
	Primitive *ep;			/* primitives to raster */
	BPrimitive prim, *p, *cp;
	Rectangle *wr, bbox;
	Channel **taskchans;
	ulong nproc;
	int i, np;

	tp = arg;
	threadsetname("tiler %d", tp->id);

	cp = _emalloc(16*sizeof(*cp));
	taskchans = tp->taskchans;
	nproc = tp->nproc;
	wr = _emalloc(nproc*sizeof(Rectangle));

	memset(&vsp, 0, sizeof vsp);
	vsp.getuniform = sparams_getuniform;
	vsp.getattr = sparams_getattr;
	vsp.setattr = sparams_setattr;
	vsp.toraster = nil;

	while((params = recvp(tp->paramsc)) != nil){
		if(params->job->rctl->doprof
		&& params->job->times.Tn[tp->id].t0 == 0)
			params->job->times.Tn[tp->id].t0 = nanosec();

		if(params->op == OP_END){
			if(params->job->rctl->doprof)
				params->job->times.Tn[tp->id].t1 = nanosec();

			if(decref(params->job) < 1){
				if(params->job->camera->rendopts & ROAbuff)
					initworkrects(wr, nproc, &params->job->fb->r);

				params->job->ref = nproc;
				for(i = 0; i < nproc; i++){
					task = _emalloc(sizeof *task);
					task->SUparams = *params;
					if(params->job->camera->rendopts & ROAbuff)
						task->wr = wr[i];
					sendp(taskchans[i], task);
				}
				free(params);
			}
			continue;
		}
		vsp.su = params;

		initworkrects(wr, nproc, &params->fb->r);

		for(ep = params->eb; ep != params->ee; ep++){
			np = 1;	/* start with one. after clipping it might change */

			p = assembleprim(&prim, ep, params->entity->mdl);

			switch(p->type){
			case PPoint:
				p->v[0].mtl = p->mtl;
				p->v[0].attrs = nil;
				p->v[0].nattrs = 0;

				vsp.v = &p->v[0];
				vsp.idx = 0;
				p->v[0].p = params->stab->vs(&vsp);

				if(!isvisible(p->v[0].p))
					break;

				p->v[0].p = clip2ndc(p->v[0].p);
				p->v[0].p = ndc2viewport(params->fb, p->v[0].p);

				bbox.min.x = p->v[0].p.x;
				bbox.min.y = p->v[0].p.y;

				for(i = 0; i < nproc; i++)
					if(ptinrect(bbox.min, wr[i])){
						task = _emalloc(sizeof *task);
						task->SUparams = *params;
						task->op = OP_RASTER;
						task->clipr = &params->job->cliprects[i];
						task->p = *p;
						task->p.v[0] = _dupvertex(&p->v[0]);
						sendp(taskchans[i], task);
						break;
					}
				_delvattrs(&p->v[0]);
				break;
			case PLine:
				for(i = 0; i < 2; i++){
					p->v[i].mtl = p->mtl;
					p->v[i].attrs = nil;
					p->v[i].nattrs = 0;

					vsp.v = &p->v[i];
					vsp.idx = i;
					p->v[i].p = params->stab->vs(&vsp);
				}

				if(!isvisible(p->v[0].p) || !isvisible(p->v[1].p)){
					np = _clipprimitive(p, cp);
					p = cp;
				}

				if(np < 1)
					break;

				p->v[0].p = clip2ndc(p->v[0].p);
				p->v[1].p = clip2ndc(p->v[1].p);
				p->v[0].p = ndc2viewport(params->fb, p->v[0].p);
				p->v[1].p = ndc2viewport(params->fb, p->v[1].p);

				bbox.min.x = min(p->v[0].p.x, p->v[1].p.x);
				bbox.min.y = min(p->v[0].p.y, p->v[1].p.y);
				bbox.max.x = max(p->v[0].p.x, p->v[1].p.x)+1;
				bbox.max.y = max(p->v[0].p.y, p->v[1].p.y)+1;

				for(i = 0; i < nproc; i++)
					if(rectXrect(bbox, wr[i])){
						task = _emalloc(sizeof *task);
						task->SUparams = *params;
						task->op = OP_RASTER;
						task->wr = wr[i];
						task->clipr = &params->job->cliprects[i];
						task->p = *p;
						task->p.v[0] = _dupvertex(&p->v[0]);
						task->p.v[1] = _dupvertex(&p->v[1]);
						sendp(taskchans[i], task);
					}
				_delvattrs(&p->v[0]);
				_delvattrs(&p->v[1]);
				break;
			case PTriangle:
				for(i = 0; i < 3; i++){
					p->v[i].mtl = p->mtl;
					p->v[i].attrs = nil;
					p->v[i].nattrs = 0;
					p->v[i].tangent = p->tangent;

					vsp.v = &p->v[i];
					vsp.idx = i;
					p->v[i].p = params->stab->vs(&vsp);
				}

				if(!isvisible(p->v[0].p) || !isvisible(p->v[1].p) || !isvisible(p->v[2].p)){
					np = _clipprimitive(p, cp);
					p = cp;
				}

				for(; np--; p++){
					p->v[0].p = clip2ndc(p->v[0].p);
					p->v[1].p = clip2ndc(p->v[1].p);
					p->v[2].p = clip2ndc(p->v[2].p);

					/* culling */
					if((params->camera->cullmode == CullFront && !isfacingback(p))
					|| (params->camera->cullmode == CullBack && isfacingback(p)))
						goto skiptri;

					p->v[0].p = ndc2viewport(params->fb, p->v[0].p);
					p->v[1].p = ndc2viewport(params->fb, p->v[1].p);
					p->v[2].p = ndc2viewport(params->fb, p->v[2].p);

					bbox.min.x = min(min(p->v[0].p.x, p->v[1].p.x), p->v[2].p.x);
					bbox.min.y = min(min(p->v[0].p.y, p->v[1].p.y), p->v[2].p.y);
					bbox.max.x = max(max(p->v[0].p.x, p->v[1].p.x), p->v[2].p.x)+1;
					bbox.max.y = max(max(p->v[0].p.y, p->v[1].p.y), p->v[2].p.y)+1;

					for(i = 0; i < nproc; i++)
						if(rectXrect(bbox, wr[i])){
							task = _emalloc(sizeof *task);
							task->SUparams = *params;
							task->op = OP_RASTER;
							task->wr = bbox;
							rectclip(&task->wr, wr[i]);
							task->clipr = &params->job->cliprects[i];
							task->p = *p;
							task->p.v[0] = _dupvertex(&p->v[0]);
							task->p.v[1] = _dupvertex(&p->v[1]);
							task->p.v[2] = _dupvertex(&p->v[2]);
							sendp(taskchans[i], task);
						}
skiptri:
					_delvattrs(&p->v[0]);
					_delvattrs(&p->v[1]);
					_delvattrs(&p->v[2]);
				}
				break;
			default: sysfatal("alien primitive detected");
			}
		}
		free(params);
	}
}

static void
entityproc(void *arg)
{
	Entityparam *ep;
	Channel *paramsin, **paramsout, **taskchans;
	Tilerparam *tp;
	Rasterparam *rp;
	SUparams *params, *newparams;
	Primitive *eb, *ee;
	ulong stride, nprims, nproc, nworkers;
	int i;

	threadsetname("entityproc");

	ep = arg;
	paramsin = ep->paramsc;

	nproc = ep->rctl->nprocs;
	if(nproc > 2)
		nproc /= 2;

	paramsout = _emalloc(nproc*sizeof(*paramsout));
	taskchans = _emalloc(nproc*sizeof(*taskchans));
	for(i = 0; i < nproc; i++){
		paramsout[i] = chancreate(sizeof(SUparams*), 256);
		tp = _emalloc(sizeof *tp);
		tp->id = i;
		tp->paramsc = paramsout[i];
		tp->taskchans = taskchans;
		tp->nproc = nproc;
		proccreate(tiler, tp, mainstacksize);
	}
	for(i = 0; i < nproc; i++){
		rp = _emalloc(sizeof *rp);
		rp->id = i;
		rp->taskc = taskchans[i] = chancreate(sizeof(Rastertask*), 512);
		proccreate(rasterizer, rp, mainstacksize);
	}

	while((params = recvp(paramsin)) != nil){
		if(params->job->rctl->doprof && params->job->times.E.t0 == 0)
			params->job->times.E.t0 = nanosec();

		/* prof: initialize timing slots for the next stages */
		if(params->job->rctl->doprof && params->job->times.Tn == nil){
			assert(params->job->times.Rn == nil);
			params->job->times.Tn = _emalloc(nproc*sizeof(Rendertime));
			params->job->times.Rn = _emalloc(nproc*sizeof(Rendertime));
			memset(params->job->times.Tn, 0, nproc*sizeof(Rendertime));
			memset(params->job->times.Rn, 0, nproc*sizeof(Rendertime));
		}

		if(params->op == OP_END){
			params->job->ref = nproc;
			for(i = 0; i < nproc; i++)
				sendp(paramsout[i], params);
			if(params->job->rctl->doprof)
				params->job->times.E.t1 = nanosec();
			continue;
		}

		if(params->job->cliprects == nil){
			params->job->cliprects = _emalloc(nproc*sizeof(Rectangle));
			params->job->ncliprects = nproc;
			for(i = 0; i < nproc; i++){
				params->job->cliprects[i].min = (Point){-1,-1};
				params->job->cliprects[i].max = (Point){-1,-1};
			}
		}

		eb = params->entity->mdl->prims->items;
		nprims = params->entity->mdl->prims->nitems;
		ee = eb + nprims;

		if(nprims <= nproc){
			nworkers = nprims;
			stride = 1;
		}else{
			nworkers = nproc;
			stride = nprims/nproc;
		}

		for(i = 0; i < nworkers; i++){
			newparams = _emalloc(sizeof *newparams);
			*newparams = *params;
			newparams->op = OP_PRIMS;
			newparams->eb = eb + i*stride;
			newparams->ee = i == nworkers-1? ee: newparams->eb + stride;
			sendp(paramsout[i], newparams);
		}
		free(params);
	}
}

static void
renderer(void *arg)
{
	Renderer *rctl;
	Renderjob *job;
	Scene *sc;
	Entity *ent;
	SUparams *params;
	Entityparam *ep;
	uvlong lastid;

	threadsetname("renderer");

	rctl = arg;
	lastid = 0;

	ep = _emalloc(sizeof *ep);
	ep->rctl = rctl;
	ep->paramsc = chancreate(sizeof(SUparams*), 256);
	proccreate(entityproc, ep, mainstacksize);

	while((job = recvp(rctl->jobq)) != nil){
		if(job->rctl->doprof)
			job->times.R.t0 = nanosec();

		job->id = lastid++;
		sc = job->camera->scene;
		if(sc->nents < 1){
			nbsend(job->donec, nil);
			continue;
		}

		if(job->camera->rendopts & ROAbuff)
			initAbuf(job->fb);

		for(ent = sc->ents.next; ent != &sc->ents; ent = ent->next){
			params = _emalloc(sizeof *params);
			memset(params, 0, sizeof *params);
			params->fb = job->fb;
			params->stab = job->shaders;
			params->job = job;
			params->camera = job->camera;
			params->entity = ent;
			params->op = OP_ENTITY;
			sendp(ep->paramsc, params);
		}

		/* mark end of job */
		params = _emalloc(sizeof *params);
		params->job = job;
		params->op = OP_END;
		sendp(ep->paramsc, params);

		if(job->rctl->doprof)
			job->times.R.t1 = nanosec();
	}
}

Renderer *
initgraphics(void)
{
	Renderer *r;
	char *nprocs;
	ulong nproc;

	nprocs = getenv("NPROC");
	if(nprocs == nil || (nproc = strtoul(nprocs, nil, 10)) < 2)
		nproc = 1;
	free(nprocs);

	r = _emalloc(sizeof *r);
	memset(r, 0, sizeof *r);
	r->jobq = chancreate(sizeof(Renderjob*), 8);
	r->nprocs = nproc;
	proccreate(renderer, r, mainstacksize);
	return r;
}
