enum {
	ε1 = 1e-5,
	ε2 = 1e-6,

	/* shader unit operations */
	OP_ENTITY = 0,
	OP_PRIMS,
	OP_RASTER,
	OP_SYNC,
	OP_END,
};

typedef struct BPrimitive BPrimitive;
typedef struct Polygon Polygon;
typedef struct Entityparam Entityparam;
typedef struct Tilerparam Tilerparam;
typedef struct Rasterparam Rasterparam;
typedef struct Rastertask Rastertask;
typedef struct fGradient fGradient;
typedef struct pGradient pGradient;
typedef struct vGradient vGradient;

struct BPrimitive
{
	int type;
	BVertex v[3];
	Material *mtl;
	Point3 tangent;		/* used for normal mapping */
};

struct Polygon
{
	BVertex *v;
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
	BPrimitive p;
};

struct fGradient
{
	double f0;
	double dx;
	double dy;
};

struct pGradient
{
	Point3 p0;
	Point3 dx;
	Point3 dy;
};

struct vGradient
{
	BVertex v0;
	BVertex dx;
	BVertex dy;
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
BVertex _dupvertex(BVertex*);
void _loadvertex(BVertex*, BVertex*);
void _lerpvertex(BVertex*, BVertex*, BVertex*, double);
void _berpvertex(BVertex*, BVertex*, BVertex*, BVertex*, Point3);
void _addvertex(BVertex*, BVertex*);
void _mulvertex(BVertex*, double);
void _delvattrs(BVertex*);
void _fprintvattrs(int, BVertex*);
void _addvattr(Vertexattrs*, char*, int, void*);
Vertexattr *_getvattr(Vertexattrs*, char*);

/* clip */
int _clipprimitive(BPrimitive*, BPrimitive*);
int _rectclipline(Rectangle, Point*, Point*, BVertex*, BVertex*);

/* util */
void _memsetl(void*, ulong, usize);

#define getpixel(fb, p)		_rastergetcolor(fb, p)
#define putpixel(fb, p, c)	_rasterputcolor(fb, p, c)
#define getdepth(fb, p)		_rastergetfloat(fb, p)
#define putdepth(fb, p, z)	_rasterputfloat(fb, p, z)

/* void SWAP(type, type *a, type *b) */
#define SWAP(t, a, b) {t tmp; tmp = *(a); *(a) = *(b); *(b) = tmp;}
