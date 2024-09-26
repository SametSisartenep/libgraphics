#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

enum {
	NaI = ~0ULL,	/* not an index */
	MTLHTSIZ = 17,
};

typedef struct Curline Curline;
struct Curline
{
	char file[256];
	usize line;
};

typedef struct IArray IArray;
typedef struct Wirevert Wirevert;
typedef struct Wireprim Wireprim;
typedef struct Mtlentry Mtlentry;
typedef struct Mtltab Mtltab;

struct IArray
{
	void *items;
	usize nitems;
	usize itemsize;
};

struct Wirevert
{
	usize p, n, t, c;
};

struct Wireprim
{
	int nv;
	usize v[3];
	usize T;
	char *mtlname;
};

struct Mtlentry
{
	Material;
	ulong idx;
	Mtlentry *next;
};

struct Mtltab
{
	Mtlentry *mtls[MTLHTSIZ];
	ulong nmtls;
	int loaded;			/* was the table loaded into a model already? */
};

static void
error(Curline *l, char *fmt, ...)
{
	va_list va;
	char buf[ERRMAX], *bp;

	bp = seprint(buf, buf + sizeof buf, "%s:%llud ", l->file, l->line);

	va_start(va, fmt);
	vseprint(bp, buf + sizeof buf, fmt, va);
	va_end(va);

	werrstr("%s", buf);
}

static IArray *
mkitemarray(usize is)
{
	IArray *a;

	a = emalloc(sizeof *a);
	memset(a, 0, sizeof *a);
	a->itemsize = is;
	return a;
}

static usize
itemarrayadd(IArray *a, void *i, int dedup)
{
	char *p;
	usize idx;

	if(dedup){
		p = a->items;
		for(idx = 0; idx < a->nitems; idx++)
			if(memcmp(i, &p[idx*a->itemsize], a->itemsize) == 0)
				return idx;
	}

	idx = a->nitems;
	a->items = erealloc(a->items, ++a->nitems * a->itemsize);
	p = a->items;
	p += idx*a->itemsize;
	memmove(p, i, a->itemsize);
	return idx;
}

static void *
itemarrayget(IArray *a, usize idx)
{
	char *p;

	if(idx >= a->nitems)
		return nil;

	p = a->items;
	p += idx*a->itemsize;
	return p;
}

static void
rmitemarray(IArray *a)
{
	free(a->items);
	free(a);
}

static uint
hash(char *s)
{
	uint h;

	h = 0x811c9dc5;
	while(*s != 0)
		h = (h^(uchar)*s++) * 0x1000193;
	return h % MTLHTSIZ;
}

static Mtltab *
mkmtltab(void)
{
	Mtltab *t;

	t = emalloc(sizeof *t);
	memset(t, 0, sizeof *t);
	return t;
}

static void
freemtlentry(Mtlentry *m)
{
	freetexture(m->normalmap);
	freetexture(m->specularmap);
	freetexture(m->diffusemap);
	free(m->name);
	free(m);
}

static Mtlentry *
mtltabadd(Mtltab *t, Material *m)
{
	Mtlentry *nm, *mp, *prev;
	uint h;

	nm = emalloc(sizeof *nm);
	memset(nm, 0, sizeof *nm);
	nm->Material = *m;
	nm->next = nil;

	prev = nil;
	h = hash(nm->name);
	for(mp = t->mtls[h]; mp != nil; prev = mp, mp = mp->next)
		if(strcmp(mp->name, nm->name) == 0){
			werrstr("material already exists");
			return nil;
		}
	if(prev == nil){
		t->mtls[h] = nm;
		t->nmtls++;
		return nm;
	}
	prev->next = nm;
	t->nmtls++;
	return nm;
}

static Mtlentry *
mtltabget(Mtltab *t, char *name)
{
	Mtlentry *m;
	uint h;

	h = hash(name);
	for(m = t->mtls[h]; m != nil; m = m->next)
		if(strcmp(m->name, name) == 0)
			break;
	return m;
}

static void
mtltabloadmodel(Model *m, Mtltab *t)
{
	Mtlentry *e;
	int i;

	for(i = 0; i < nelem(t->mtls); i++)
		for(e = t->mtls[i]; e != nil; e = e->next)
			e->idx = m->addmaterial(m, *e);
	t->loaded++;
}

static void
rmmtltab(Mtltab *t)
{
	Mtlentry *m, *nm;
	int i;

	for(i = 0; i < nelem(t->mtls); i++)
		for(m = t->mtls[i]; m != nil; m = nm){
			nm = m->next;
			if(t->loaded)
				free(m);
			else
				freemtlentry(m);
		}
}

/*
 * TODO if materials are inserted between primitive declarations
 * references to those materials from early primitives can cause
 * out-of-bounds accesses. find a solution.
 *
 * example:
 *
 *	mtl A {
 *		diffuse: 1 0 0
 *	}
 *	prim ... A
 *	mtl B {
 *		diffuse: 1 0 0
 *	}
 *	prim ... B
 *
 * now the reference to A is probably wrong because of realloc.
 */
Model *
readmodel(int fd)
{
	Curline curline;
	IArray *pa, *na, *ta, *ca, *Ta, *va, *prima;
	Mtltab *mtltab;
	Mtlentry *me;
	Point3 p, n, T;
	Point2 t;
	Color c;
	Vertex v;
	Primitive prim;
	Material mtl;
	Model *m;
	Memimage *mi;
	Biobuf *bin;
	void *vp;
	char *line, *f[10], *s, assets[200], buf[256];
	usize idx, i;
	ulong primidx;
	int nf, nv, inamaterial, texfd;

	n.w = T.w = 0;
	t.w = 1;
	m = nil;

	bin = Bfdopen(fd, OREAD);
	if(bin == nil)
		sysfatal("Bfdopen: %r");

	pa = mkitemarray(sizeof(p));
	na = mkitemarray(sizeof(n));
	ta = mkitemarray(sizeof(t));
	ca = mkitemarray(sizeof(c));
	Ta = mkitemarray(sizeof(T));
	va = mkitemarray(sizeof(v));
	prima = mkitemarray(sizeof(prim));
	mtltab = mkmtltab();

	memset(&curline, 0, sizeof curline);
	if(fd2path(fd, curline.file, sizeof curline.file) != 0)
		sysfatal("fd2path: %r");
	if((s = strrchr(curline.file, '/')) != nil){
		*s = 0;
		snprint(assets, sizeof assets, "%s", curline.file);
		memmove(curline.file, s+1, strlen(s+1) + 1);
	}else{
		assets[0] = '.';
		assets[1] = 0;
	}
	inamaterial = 0;

	while((line = Brdline(bin, '\n')) != nil){
		line[Blinelen(bin)-1] = 0;
		curline.line++;

		nf = tokenize(line, f, nelem(f));
		if(nf < 1)
			continue;

		if(inamaterial){
			if((s = strchr(f[0], ':')) != nil)
				*s = 0;

			if(strcmp(f[0], "}") == 0){
				if(mtltabadd(mtltab, &mtl) == nil){
					error(&curline, "mtltabadd: %r");
					goto getout;
				}
				inamaterial--;
			}else if(strcmp(f[0], "ambient") == 0){
				if(nf != 4 && nf != 5){
					error(&curline, "syntax error");
					goto getout;
				}
				mtl.ambient.r = strtod(f[1], nil);
				mtl.ambient.g = strtod(f[2], nil);
				mtl.ambient.b = strtod(f[3], nil);
				mtl.ambient.a = nf == 5? strtod(f[4], nil): 1;
			}else if(strcmp(f[0], "diffuse") == 0){
				if(nf != 4 && nf != 5){
					error(&curline, "syntax error");
					goto getout;
				}

				mtl.diffuse.r = strtod(f[1], nil);
				mtl.diffuse.g = strtod(f[2], nil);
				mtl.diffuse.b = strtod(f[3], nil);
				mtl.diffuse.a = nf == 5? strtod(f[4], nil): 1;
			}else if(strcmp(f[0], "diffusemap") == 0){
				if(nf != 2){
					error(&curline, "syntax error");
					goto getout;
				}
				if(mtl.diffusemap != nil){
					error(&curline, "there is already a diffuse map");
					goto getout;
				}

				snprint(buf, sizeof buf, "%s/%s", assets, f[1]);
				texfd = open(buf, OREAD);
				if(texfd < 0){
notexture:
					error(&curline, "could not read texture '%s'", f[1]);
					goto getout;
				}
				mi = readmemimage(texfd);
				if(mi == nil){
					close(texfd);
					goto notexture;
				}
				mtl.diffusemap = alloctexture(sRGBTexture, mi);
				close(texfd);
			}else if(strcmp(f[0], "specular") == 0){
				if(nf != 4 && nf != 5){
					error(&curline, "syntax error");
					goto getout;
				}

				mtl.specular.r = strtod(f[1], nil);
				mtl.specular.g = strtod(f[2], nil);
				mtl.specular.b = strtod(f[3], nil);
				mtl.specular.a = nf == 5? strtod(f[4], nil): 1;
			}else if(strcmp(f[0], "specularmap") == 0){
				if(nf != 2){
					error(&curline, "syntax error");
					goto getout;
				}
				if(mtl.specularmap != nil){
					error(&curline, "there is already a specular map");
					goto getout;
				}

				snprint(buf, sizeof buf, "%s/%s", assets, f[1]);
				texfd = open(buf, OREAD);
				if(texfd < 0)
					goto notexture;
				mi = readmemimage(texfd);
				if(mi == nil){
					close(texfd);
					goto notexture;
				}
				mtl.specularmap = alloctexture(RAWTexture, mi);
				close(texfd);
			}else if(strcmp(f[0], "shininess") == 0){
				if(nf != 2){
					error(&curline, "syntax error");
					goto getout;
				}
				mtl.shininess = strtod(f[1], nil);
			}else if(strcmp(f[0], "normals") == 0){
				if(nf != 2){
					error(&curline, "syntax error");
					goto getout;
				}
				if(mtl.normalmap != nil){
					error(&curline, "there is already a normal map");
					goto getout;
				}

				snprint(buf, sizeof buf, "%s/%s", assets, f[1]);
				texfd = open(buf, OREAD);
				if(texfd < 0)
					goto notexture;
				mi = readmemimage(texfd);
				if(mi == nil){
					close(texfd);
					goto notexture;
				}
				mtl.normalmap = alloctexture(RAWTexture, mi);
				close(texfd);
			}else{
				error(&curline, "unknown mtl parameter '%s'", f[0]);
				goto getout;
			}

			continue;
		}

		if(strcmp(f[0], "p") == 0){
			if(nf != 4 && nf != 5){
				error(&curline, "syntax error");
				goto getout;
			}
			p.x = strtod(f[1], nil);
			p.y = strtod(f[2], nil);
			p.z = strtod(f[3], nil);
			p.w = nf == 5? strtod(f[4], nil): 1;
			itemarrayadd(pa, &p, 0);
		}else if(strcmp(f[0], "n") == 0){
			if(nf != 4){
				error(&curline, "syntax error");
				goto getout;
			}
			n.x = strtod(f[1], nil);
			n.y = strtod(f[2], nil);
			n.z = strtod(f[3], nil);
			itemarrayadd(na, &n, 0);
		}else if(strcmp(f[0], "t") == 0){
			if(nf != 3){
				error(&curline, "syntax error");
				goto getout;
			}
			t.x = strtod(f[1], nil);
			t.y = strtod(f[2], nil);
			itemarrayadd(ta, &t, 0);
		}else if(strcmp(f[0], "c") == 0){
			if(nf != 4 && nf != 5){
				error(&curline, "syntax error");
				goto getout;
			}
			c.r = strtod(f[1], nil);
			c.g = strtod(f[2], nil);
			c.b = strtod(f[3], nil);
			c.a = nf == 5? strtod(f[4], nil): 1;
			itemarrayadd(ca, &c, 0);
		}else if(strcmp(f[0], "T") == 0){
			if(nf != 4){
				error(&curline, "syntax error");
				goto getout;
			}
			T.x = strtod(f[1], nil);
			T.y = strtod(f[2], nil);
			T.z = strtod(f[3], nil);
			itemarrayadd(Ta, &T, 0);
		}else if(strcmp(f[0], "v") == 0){
			if(nf != 5){
				error(&curline, "syntax error");
				goto getout;
			}
			memset(&v, 0, sizeof v);

			if(strcmp(f[1], "-") == 0){
				error(&curline, "vertex has no position");
				goto getout;
			}
			idx = strtoul(f[1], nil, 10);
			vp = itemarrayget(pa, idx);
			if(vp == nil){
				error(&curline, "no position at idx %llud", idx);
				goto getout;
			}
			v.p = *(Point3*)vp;

			if(strcmp(f[2], "-") != 0){
				idx = strtoul(f[2], nil, 10);
				vp = itemarrayget(na, idx);
				if(vp == nil){
					error(&curline, "no normal at idx %llud", idx);
					goto getout;
				}
				v.n = *(Point3*)vp;
			}

			if(strcmp(f[3], "-") != 0){
				idx = strtoul(f[3], nil, 10);
				vp = itemarrayget(ta, idx);
				if(vp == nil){
					error(&curline, "no texture at idx %llud", idx);
					goto getout;
				}
				v.uv = *(Point2*)vp;
			}

			if(strcmp(f[4], "-") != 0){
				idx = strtoul(f[4], nil, 10);
				vp = itemarrayget(ca, idx);
				if(vp == nil){
					error(&curline, "no color at idx %llud", idx);
					goto getout;
				}
				v.c = *(Color*)vp;
			}

			itemarrayadd(va, &v, 0);
		}else if(strcmp(f[0], "prim") == 0){
			if(nf < 3 || nf > 7){
				error(&curline, "syntax error");
				goto getout;
			}
			memset(&prim, 0, sizeof prim);

			nv = strtoul(f[1], nil, 10);
			switch(nv-1){
			case PPoint:
				prim.type = PPoint;

				idx = strtoul(f[2], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil){
novertex:
					error(&curline, "no vertex at idx %llud", idx);
					goto getout;
				}
				prim.v[0] = *(Vertex*)vp;

				/* ignore 4th field (nf == 4) */

				if(nf == 5){
					prim.mtl = mtltabget(mtltab, f[4]);
					if(prim.mtl == nil){
						error(&curline, "material '%s' not found", f[4]);
						goto getout;
					}
				}
				break;
			case PLine:
				prim.type = PLine;

				idx = strtoul(f[2], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				prim.v[0] = *(Vertex*)vp;

				if(nf < 4){
notenough:
					error(&curline, "not enough prim vertices");
					goto getout;
				}
				idx = strtoul(f[3], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				prim.v[1] = *(Vertex*)vp;

				/* ignore 5th field (nf == 5) */

				if(nf == 6){
					prim.mtl = mtltabget(mtltab, f[5]);
					if(prim.mtl == nil){
						error(&curline, "material '%s' not found", f[5]);
						goto getout;
					}
				}
				break;
			case PTriangle:
				prim.type = PTriangle;

				idx = strtoul(f[2], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				prim.v[0] = *(Vertex*)vp;

				if(nf < 4)
					goto notenough;
				idx = strtoul(f[3], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				prim.v[1] = *(Vertex*)vp;

				if(nf < 5)
					goto notenough;
				idx = strtoul(f[4], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				prim.v[2] = *(Vertex*)vp;

				if(nf < 6){
					error(&curline, "missing triangle tangent field");
					goto getout;
				}
				if(strcmp(f[5], "-") != 0){
					idx = strtoul(f[5], nil, 10);
					vp = itemarrayget(Ta, idx);
					if(vp == nil){
						error(&curline, "no tangent at idx %llud", idx);
						goto getout;
					}
					prim.tangent = *(Point3*)vp;
				}

				if(nf == 7){
					prim.mtl = mtltabget(mtltab, f[6]);
					if(prim.mtl == nil){
						error(&curline, "material '%s' not found", f[6]);
						goto getout;
					}
				}
				break;
			default:
				error(&curline, "alien primitive detected");
				goto getout;
			}

			itemarrayadd(prima, &prim, 0);
		}else if(strcmp(f[0], "mtl") == 0){
			if(nf != 3 || strcmp(f[2], "{") != 0){
				error(&curline, "syntax error");
				goto getout;
			}
			memset(&mtl, 0, sizeof mtl);

			mtl.name = strdup(f[1]);
			if(mtl.name == nil)
				sysfatal("strdup: %r");
			inamaterial++;
		}else{
			error(&curline, "syntax error");
			goto getout;
		}
	}

	if(prima->nitems < 1){
		werrstr("no primitives no model");
		goto getout;
	}

	m = newmodel();
	mtltabloadmodel(m, mtltab);
	for(i = 0; i < prima->nitems; i++){
		primidx = m->addprim(m, *(Primitive*)itemarrayget(prima, i));
		if(m->prims[primidx].mtl != nil){
			me = mtltabget(mtltab, m->prims[primidx].mtl->name);
			m->prims[primidx].mtl = &m->materials[me->idx];
		}
	}

getout:
	rmitemarray(pa);
	rmitemarray(na);
	rmitemarray(ta);
	rmitemarray(ca);
	rmitemarray(Ta);
	rmitemarray(va);
	rmitemarray(prima);
	rmmtltab(mtltab);
	Bterm(bin);
	return m;
}

static int
Bprintp3(Biobuf *b, Point3 *p)
{
	int n;

	n = Bprint(b, "%g %g %g", p->x, p->y, p->z);
	if(p->w != 1)
		n += Bprint(b, " %g", p->w);
	n += Bprint(b, "\n");
	return n;
}

static int
Bprintp2(Biobuf *b, Point2 *p)
{
	int n;

	n = Bprint(b, "%g %g", p->x, p->y);
	if(p->w != 1)
		n += Bprint(b, " %g", p->w);
	n += Bprint(b, "\n");
	return n;
}

static int
Bprintn3(Biobuf *b, Point3 *p)
{
	return Bprint(b, "%g %g %g\n", p->x, p->y, p->z);
}

static int
Bprintp(Biobuf *b, Point3 *p)
{
	int n;

	n = Bprint(b, "p ");
	n += Bprintp3(b, p);
	return n;
}

static int
Bprintn(Biobuf *b, Point3 *p)
{
	int n;

	n = Bprint(b, "n ");
	n += Bprintn3(b, p);
	return n;
}

static int
Bprintt(Biobuf *b, Point2 *p)
{
	int n;

	n = Bprint(b, "t ");
	n += Bprintp2(b, p);
	return n;
}

static int
Bprintc(Biobuf *b, Point3 *p)
{
	int n;

	n = Bprint(b, "c ");
	n += Bprintp3(b, p);
	return n;
}

static int
BprintT(Biobuf *b, Point3 *p)
{
	int n;

	n = Bprint(b, "T ");
	n += Bprintn3(b, p);
	return n;
}

static int
Bprintidx(Biobuf *b, usize idx)
{
	if(idx == NaI)
		return Bprint(b, " -");
	return Bprint(b, " %llud", idx);
}

static int
Bprintv(Biobuf *b, Wirevert *v)
{
	int n;

	n = Bprint(b, "v %llud", v->p);
	n += Bprintidx(b, v->n);
	n += Bprintidx(b, v->t);
	n += Bprintidx(b, v->c);
	n += Bprint(b, "\n");
	return n;
}

static int
Bprintprim(Biobuf *b, Wireprim *p)
{
	char *s;
	int n, i;

	n = Bprint(b, "prim %d", p->nv);
	for(i = 0; i < p->nv; i++)
		n += Bprintidx(b, p->v[i]);
	n += Bprintidx(b, p->T);
	if(p->mtlname != nil){
		s = quotestrdup(p->mtlname);
		if(s == nil)
			sysfatal("quotestrdup: %r");
		n += Bprint(b, " %s", s);
		free(s);
	}
	n += Bprint(b, "\n");
	return n;
}

/* TODO how do we deal with textures? embedded? keep a path? */
static int
Bprintmtl(Biobuf *b, Material *m)
{
	char *s;
	int n;

	s = quotestrdup(m->name);
	if(s == nil)
		sysfatal("quotestrdup: %r");
	n = Bprint(b, "mtl %s {\n", s);
	free(s);

	if(m->ambient.a > 0){
		n += Bprint(b, "\tambient: ");
		n += Bprint(b, "%g %g %g", m->ambient.r, m->ambient.g, m->ambient.b);
		if(m->ambient.a != 1)
			n += Bprint(b, " %g", m->ambient.a);
		n += Bprint(b, "\n");
	}

	if(m->diffuse.a > 0 || m->diffusemap != nil){
		n += Bprint(b, "\tdiffuse: ");
		n += Bprint(b, "%g %g %g", m->diffuse.r, m->diffuse.g, m->diffuse.b);
		if(m->diffuse.a != 1)
			n += Bprint(b, " %g", m->diffuse.a);
		n += Bprint(b, "\n");
	}

	if(m->specular.a > 0 || m->specularmap != nil){
		n += Bprint(b, "\tspecular: ");
		n += Bprint(b, "%g %g %g", m->specular.r, m->specular.g, m->specular.b);
		if(m->specular.a != 1)
			n += Bprint(b, " %g", m->specular.a);
		n += Bprint(b, "\n");
	}

	if(m->shininess > 0){
		n += Bprint(b, "\tshininess: ");
		n += Bprint(b, "%g\n", m->shininess);
	}

//	if(m->diffusemap != nil){
//		n += Bprint("\tdiffusemap: ");
//		n += Bprint(b, "%s\n", m
//	}

//	if(m->specularmap != nil){
//		n += Bprint(b, "\tspecularmap: ");
//		n += Bprint(b, "%s\n", m
//	}

//	if(m->normalmap != nil)
//		n += Bprint(b, "\tnormals: ");

	n += Bprint(b, "}\n");
	return n;
}

usize
writemodel(int fd, Model *m)
{
	IArray *pa, *na, *ta, *ca, *Ta, *va, *prima;
	Wirevert v;
	Wireprim prim;
	Primitive *p, *ep;
	Biobuf *out;
	usize n;
	int i;

	out = Bfdopen(fd, OWRITE);
	if(out == nil)
		sysfatal("Bfdopen: %r");

	pa = mkitemarray(sizeof(Point3));
	na = mkitemarray(sizeof(Point3));
	ta = mkitemarray(sizeof(Point2));
	ca = mkitemarray(sizeof(Color));
	Ta = mkitemarray(sizeof(Point3));
	va = mkitemarray(sizeof(Wirevert));
	prima = mkitemarray(sizeof(Wireprim));

	n = 0;
	p = m->prims;
	ep = p + m->nprims;

	while(p < ep){
		memset(&prim, 0, sizeof prim);

		prim.nv = p->type+1;
		for(i = 0; i < prim.nv; i++){
			v.p = itemarrayadd(pa, &p->v[i].p, 1);
			v.n = eqpt3(p->v[i].n, Vec3(0,0,0))?
				NaI: itemarrayadd(na, &p->v[i].n, 1);
			v.t = p->v[i].uv.w != 1?
				NaI: itemarrayadd(ta, &p->v[i].uv, 1);
			v.c = p->v[i].c.a == 0?
				NaI: itemarrayadd(ca, &p->v[i].c, 1);
			prim.v[i] = itemarrayadd(va, &v, 1);
		}
		prim.T = eqpt3(p->tangent, Vec3(0,0,0))?
			NaI: itemarrayadd(Ta, &p->tangent, 1);
		prim.mtlname = p->mtl != nil? p->mtl->name: nil;

		itemarrayadd(prima, &prim, 1);
		p++;
	}

	for(i = 0; i < m->nmaterials; i++)
		n += Bprintmtl(out, &m->materials[i]);

	for(i = 0; i < pa->nitems; i++)
		n += Bprintp(out, itemarrayget(pa, i));
	for(i = 0; i < na->nitems; i++)
		n += Bprintn(out, itemarrayget(na, i));
	for(i = 0; i < ta->nitems; i++)
		n += Bprintt(out, itemarrayget(ta, i));
	for(i = 0; i < ca->nitems; i++)
		n += Bprintc(out, itemarrayget(ca, i));
	for(i = 0; i < Ta->nitems; i++)
		n += BprintT(out, itemarrayget(Ta, i));
	for(i = 0; i < va->nitems; i++)
		n += Bprintv(out, itemarrayget(va, i));
	for(i = 0; i < prima->nitems; i++)
		n += Bprintprim(out, itemarrayget(prima, i));

	rmitemarray(pa);
	rmitemarray(na);
	rmitemarray(ta);
	rmitemarray(ca);
	rmitemarray(Ta);
	rmitemarray(va);
	rmitemarray(prima);
	Bterm(out);
	return n;
}
