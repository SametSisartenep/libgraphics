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
	MTLHTSIZ = 17,
};

typedef struct Curline Curline;
struct Curline
{
	char file[256];
	usize line;
};

typedef struct Mtlentry Mtlentry;
typedef struct Mtltab Mtltab;

struct Mtlentry
{
	Material;
	ulong idx;
	Mtlentry *next;
};

struct Mtltab
{
	Mtlentry *mtls[MTLHTSIZ];
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

	t = _emalloc(sizeof *t);
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

	nm = _emalloc(sizeof *nm);
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
		return nm;
	}
	prev->next = nm;
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

Model *
readmodel(int fd)
{
	Curline curline;
	ItemArray *pa, *na, *ta, *ca, *Ta, *va, *Pa;
	Mtltab *mtltab;
	Mtlentry *me;
	Point3 p, n, T;
	Point2 t;
	Color c;
	Vertex v;
	Primitive P, *prim;
	Material mtl;
	Model *m;
	Memimage *mi;
	Biobuf *bin;
	void *vp;
	char *line, *f[10], *s, assets[200], buf[256];
	usize idx, i;
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
	Pa = mkitemarray(sizeof(P));
	mtltab = mkmtltab();

	memset(&curline, 0, sizeof curline);
	if(fd2path(fd, curline.file, sizeof curline.file) != 0)
		sysfatal("fd2path: %r");
	if((s = strrchr(curline.file, '/')) != nil){
		*s++ = 0;
		snprint(assets, sizeof assets, "%s", curline.file);
		memmove(curline.file, s, strlen(s) + 1);
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
			v = mkvert();

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
			v.p = idx;

			if(strcmp(f[2], "-") != 0){
				idx = strtoul(f[2], nil, 10);
				vp = itemarrayget(na, idx);
				if(vp == nil){
					error(&curline, "no normal at idx %llud", idx);
					goto getout;
				}
				v.n = idx;
			}

			if(strcmp(f[3], "-") != 0){
				idx = strtoul(f[3], nil, 10);
				vp = itemarrayget(ta, idx);
				if(vp == nil){
					error(&curline, "no texture at idx %llud", idx);
					goto getout;
				}
				v.uv = idx;
			}

			if(strcmp(f[4], "-") != 0){
				idx = strtoul(f[4], nil, 10);
				vp = itemarrayget(ca, idx);
				if(vp == nil){
					error(&curline, "no color at idx %llud", idx);
					goto getout;
				}
				v.c = idx;
			}

			itemarrayadd(va, &v, 0);
		}else if(strcmp(f[0], "P") == 0){
			if(nf < 3 || nf > 7){
				error(&curline, "syntax error");
				goto getout;
			}
			P = mkprim(-1);

			nv = strtoul(f[1], nil, 10);
			switch(nv-1){
			case PPoint:
				P.type = PPoint;

				idx = strtoul(f[2], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil){
novertex:
					error(&curline, "no vertex at idx %llud", idx);
					goto getout;
				}
				P.v[0] = idx;

				/* ignore 4th field (nf == 4) */

				if(nf == 5){
					P.mtl = mtltabget(mtltab, f[4]);
					if(P.mtl == nil){
						error(&curline, "material '%s' not found", f[4]);
						goto getout;
					}
				}
				break;
			case PLine:
				P.type = PLine;

				idx = strtoul(f[2], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				P.v[0] = idx;

				if(nf < 4){
notenough:
					error(&curline, "not enough prim vertices");
					goto getout;
				}
				idx = strtoul(f[3], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				P.v[1] = idx;

				/* ignore 5th field (nf == 5) */

				if(nf == 6){
					P.mtl = mtltabget(mtltab, f[5]);
					if(P.mtl == nil){
						error(&curline, "material '%s' not found", f[5]);
						goto getout;
					}
				}
				break;
			case PTriangle:
				P.type = PTriangle;

				idx = strtoul(f[2], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				P.v[0] = idx;

				if(nf < 4)
					goto notenough;
				idx = strtoul(f[3], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				P.v[1] = idx;

				if(nf < 5)
					goto notenough;
				idx = strtoul(f[4], nil, 10);
				vp = itemarrayget(va, idx);
				if(vp == nil)
					goto novertex;
				P.v[2] = idx;

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
					P.tangent = idx;
				}

				if(nf == 7){
					P.mtl = mtltabget(mtltab, f[6]);
					if(P.mtl == nil){
						error(&curline, "material '%s' not found", f[6]);
						goto getout;
					}
				}
				break;
			default:
				error(&curline, "alien primitive detected");
				goto getout;
			}

			itemarrayadd(Pa, &P, 0);
		}else if(strcmp(f[0], "mtl") == 0){
			if(nf != 3 || strcmp(f[2], "{") != 0){
				error(&curline, "syntax error");
				goto getout;
			}
			memset(&mtl, 0, sizeof mtl);

			mtl.name = _estrdup(f[1]);
			inamaterial++;
		}else{
			error(&curline, "syntax error");
			goto getout;
		}
	}

	if(Pa->nitems < 1){
		werrstr("no primitives no model");
		goto getout;
	}

	m = newmodel();
	mtltabloadmodel(m, mtltab);
	copyitemarray(m->positions, pa);
	copyitemarray(m->normals, na);
	copyitemarray(m->texcoords, ta);
	copyitemarray(m->colors, ca);
	copyitemarray(m->tangents, Ta);
	copyitemarray(m->verts, va);
	copyitemarray(m->prims, Pa);
	for(i = 0; i < Pa->nitems; i++){
		prim = itemarrayget(m->prims, i);
		if(prim->mtl != nil){
			me = mtltabget(mtltab, prim->mtl->name);
			prim->mtl = &m->materials[me->idx];
		}
	}

getout:
	rmitemarray(pa);
	rmitemarray(na);
	rmitemarray(ta);
	rmitemarray(ca);
	rmitemarray(Ta);
	rmitemarray(va);
	rmitemarray(Pa);
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
Bprintv(Biobuf *b, Vertex *v)
{
	int n;

	n = Bprint(b, "v %llud", v->p);
	n += Bprintidx(b, v->n);
	n += Bprintidx(b, v->uv);
	n += Bprintidx(b, v->c);
	n += Bprint(b, "\n");
	return n;
}

static int
BprintP(Biobuf *b, Primitive *p)
{
	char *s;
	int n, i;

	n = Bprint(b, "P %d", p->type+1);
	for(i = 0; i < p->type+1; i++)
		n += Bprintidx(b, p->v[i]);
	n += Bprintidx(b, p->tangent);
	if(p->mtl != nil){
		s = _equotestrdup(p->mtl->name);
		n += Bprint(b, " %s", s);
		free(s);
	}
	n += Bprint(b, "\n");
	return n;
}

static int
Bprintmtl(Biobuf *b, Material *m)
{
	char *s;
	int n;

	s = _equotestrdup(m->name);
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

	if(m->diffusemap != nil && m->diffusemap->file != nil)
		n += Bprint(b, "\tdiffusemap: %s\n", m->diffusemap->file);

	if(m->specularmap != nil && m->specularmap->file != nil)
		n += Bprint(b, "\tspecularmap: %s\n", m->specularmap->file);

	if(m->normalmap != nil && m->normalmap->file != nil)
		n += Bprint(b, "\tnormals: %s\n", m->normalmap->file);

	n += Bprint(b, "}\n");
	return n;
}

usize
writemodel(int fd, Model *m)
{
	Biobuf *out;
	usize n, i;

	out = Bfdopen(fd, OWRITE);
	if(out == nil)
		sysfatal("Bfdopen: %r");

	n = 0;
	for(i = 0; i < m->nmaterials; i++)
		n += Bprintmtl(out, &m->materials[i]);

	for(i = 0; i < m->positions->nitems; i++)
		n += Bprintp(out, itemarrayget(m->positions, i));
	for(i = 0; i < m->normals->nitems; i++)
		n += Bprintn(out, itemarrayget(m->normals, i));
	for(i = 0; i < m->texcoords->nitems; i++)
		n += Bprintt(out, itemarrayget(m->texcoords, i));
	for(i = 0; i < m->colors->nitems; i++)
		n += Bprintc(out, itemarrayget(m->colors, i));
	for(i = 0; i < m->tangents->nitems; i++)
		n += BprintT(out, itemarrayget(m->tangents, i));
	for(i = 0; i < m->verts->nitems; i++)
		n += Bprintv(out, itemarrayget(m->verts, i));
	for(i = 0; i < m->prims->nitems; i++)
		n += BprintP(out, itemarrayget(m->prims, i));

	Bterm(out);
	return n;
}

static int
exporttexture(char *path, Texture *t)
{
	int fd;

	fd = create(path, OWRITE|OEXCL, 0644);
	if(fd < 0){
		werrstr("create: %r");
		return -1;
	}
	if(writememimage(fd, t->image) < 0){
		close(fd);
		werrstr("could not write '%s'", path);
		return -1;
	}
	close(fd);
	return 0;
}

int
exportmodel(char *path, Model *m)
{
	static char Esmallbuf[] = "buf too small to hold path";
	Material *mtl;
	char buf[256], *pe, *me;
	int fd, idx;

	idx = 0;

	if((pe = seprint(buf, buf + sizeof buf, "%s", path)) == nil)
		sysfatal(Esmallbuf);

	for(mtl = m->materials; mtl < m->materials+m->nmaterials; mtl++, idx++){
		if(mtl->name == nil){
			fprint(2, "warning: material #%d has no name. skipping...\n", idx);
			continue;
		}

		if((me = seprint(pe, buf + sizeof buf, "/%s", mtl->name)) == nil)
			sysfatal(Esmallbuf);

		if(mtl->diffusemap != nil){
			if(seprint(me, buf + sizeof buf, "_diffuse.pic") == nil)
				sysfatal(Esmallbuf);

			if(exporttexture(buf, mtl->diffusemap) < 0)
				fprint(2, "warning: %r\n");

//			if(mtl->diffusemap->file == nil)
				mtl->diffusemap->file = _estrdup(strrchr(buf, '/')+1);
		}

		if(mtl->specularmap != nil){
			if(seprint(me, buf + sizeof buf, "_specular.pic") == nil)
				sysfatal(Esmallbuf);

			if(exporttexture(buf, mtl->specularmap) < 0)
				fprint(2, "warning: %r\n");

//			if(mtl->specularmap->file == nil)
				mtl->specularmap->file = _estrdup(strrchr(buf, '/')+1);
		}

		if(mtl->normalmap != nil){
			if(seprint(me, buf + sizeof buf, "_normals.pic") == nil)
				sysfatal(Esmallbuf);

			if(exporttexture(buf, mtl->normalmap) < 0)
				fprint(2, "warning: %r\n");

//			if(mtl->normalmap->file == nil)
				mtl->normalmap->file = _estrdup(strrchr(buf, '/')+1);
		}
	}

	if(seprint(pe, buf + sizeof buf, "/main.mdl") == nil)
		sysfatal(Esmallbuf);

	fd = create(buf, OWRITE|OEXCL, 0644);
	if(fd < 0){
		werrstr("create: %r");
		return -1;
	}
	if(writemodel(fd, m) == 0){
		close(fd);
		werrstr("writemodel: %r");
		return -1;
	}
	close(fd);

	for(mtl = m->materials; mtl < m->materials+m->nmaterials; mtl++){
		if(mtl->name == nil)
			continue;

		if(mtl->diffusemap != nil){
			free(mtl->diffusemap->file);
			mtl->diffusemap->file = nil;
		}
		if(mtl->specularmap != nil){
			free(mtl->specularmap->file);
			mtl->specularmap->file = nil;
		}
		if(mtl->normalmap != nil){
			free(mtl->normalmap->file);
			mtl->normalmap->file = nil;
		}
	}

	return 0;
}
