enum {
	ε1 = 1e-5,
	ε2 = 1e-6,
};

typedef struct Polygon Polygon;
typedef struct Entityparam Entityparam;
typedef struct Tilerparam Tilerparam;
typedef struct Rasterparam Rasterparam;
typedef struct Rastertask Rastertask;
typedef struct pGradient pGradient;
typedef struct vGradient vGradient;

struct Polygon
{
	Vertex *v;
	ulong n;
	ulong cap;
};

struct Entityparam
{
	Renderer *rctl;
	Channel *paramsc;
};

struct Tilerparam
{
	int id;
	Channel *paramsc;
	Channel **taskchans;	/* Channel*[nproc] */
	Rectangle *wr;		/* Rectangle[nproc] */
	ulong nproc;
};

struct Rasterparam
{
	int id;
	Channel *taskc;
};

struct Rastertask
{
	SUparams *params;
	Shaderparams *fsp;
	Rectangle wr;		/* working rect */
	Rectangle *clipr;
	Primitive p;
};

struct pGradient
{
	Point3 p0;
	Point3 dx;
	Point3 dy;
};

struct vGradient
{
	Vertex v0;
	Vertex dx;
	Vertex dy;
};

/* alloc */
void *_emalloc(ulong);
void *_erealloc(void*, ulong);
Memimage *_eallocmemimage(Rectangle, ulong);

/* raster */
Raster *_allocraster(char*, Rectangle, ulong);
void _clearraster(Raster*, ulong);
void _fclearraster(Raster*, float);
uchar *_rasterbyteaddr(Raster*, Point);
void _rasterput(Raster*, Point, void*);
void _rasterget(Raster*, Point, void*);
void _rasterputcolor(Raster*, Point, ulong);
ulong _rastergetcolor(Raster*, Point);
void _rasterputfloat(Raster*, Point, float);
float _rastergetfloat(Raster*, Point);
void _freeraster(Raster*);

/* fb */
Framebuf *_mkfb(Rectangle);
void _rmfb(Framebuf*);
Framebufctl *_mkfbctl(Rectangle);
void _rmfbctl(Framebufctl*);

/* vertex */
Vertex _dupvertex(Vertex*);
void _lerpvertex(Vertex*, Vertex*, Vertex*, double);
void _berpvertex(Vertex*, Vertex*, Vertex*, Vertex*, Point3);
void _addvertex(Vertex*, Vertex*);
void _mulvertex(Vertex*, double);
void _delvattrs(Vertex*);
void _fprintvattrs(int, Vertex*);
void _addvattr(Vertexattrs*, char*, int, void*);
Vertexattr *_getvattr(Vertexattrs*, char*);

/* clip */
int _clipprimitive(Primitive*, Primitive*);
int _rectclipline(Rectangle, Point*, Point*, Vertex*, Vertex*);

/* util */
void _memsetl(void*, ulong, usize);

#define getpixel(fb, p)		_rastergetcolor(fb, p)
#define putpixel(fb, p, c)	_rasterputcolor(fb, p, c)
#define getdepth(fb, p)		_rastergetfloat(fb, p)
#define putdepth(fb, p, z)	_rasterputfloat(fb, p, z)

/* void SWAP(type, type *a, type *b) */
#define SWAP(t, a, b) {t tmp; tmp = *(a); *(a) = *(b); *(b) = tmp;}
