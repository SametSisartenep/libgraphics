enum {
	ε1 = 1e-5,
	ε2 = 1e-6,
};

typedef struct Polygon Polygon;
typedef struct Entityparam Entityparam;
typedef struct Tilerparam Tilerparam;
typedef struct Rasterparam Rasterparam;
typedef struct Rastertask Rastertask;

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
	Rectangle wr;		/* working rect */
	Rectangle *clipr;
	Primitive p;
};

/* alloc */
void *emalloc(ulong);
void *erealloc(void*, ulong);
Memimage *eallocmemimage(Rectangle, ulong);

/* fb */
Raster *allocraster(char*, Rectangle, ulong);
void clearraster(Raster*, ulong);
void fclearraster(Raster*, float);
uchar *rasterbyteaddr(Raster*, Point);
void rasterput(Raster*, Point, void*);
void rasterget(Raster*, Point, void*);
void rasterputcolor(Raster*, Point, ulong);
ulong rastergetcolor(Raster*, Point);
void rasterputfloat(Raster*, Point, float);
float rastergetfloat(Raster*, Point);
void freeraster(Raster*);
Framebuf *mkfb(Rectangle);
void rmfb(Framebuf*);
Framebufctl *mkfbctl(Rectangle);
void rmfbctl(Framebufctl*);

/* vertex */
Vertex dupvertex(Vertex*);
void lerpvertex(Vertex*, Vertex*, Vertex*, double);
void berpvertex(Vertex*, Vertex*, Vertex*, Vertex*, Point3);
void delvattrs(Vertex*);
void fprintvattrs(int, Vertex*);

/* clip */
int clipprimitive(Primitive*, Primitive*);
int rectclipline(Rectangle, Point*, Point*, Vertex*, Vertex*);

/* util */
void memsetf(void*, float, usize);
void memsetl(void*, ulong, usize);

/* nanosec */
uvlong nanosec(void);

#define getpixel(fb, p)		rastergetcolor(fb, p)
#define putpixel(fb, p, c)	rasterputcolor(fb, p, c)
#define getdepth(fb, p)		rastergetfloat(fb, p)
#define putdepth(fb, p, z)	rasterputfloat(fb, p, z)

/* void SWAP(type t, type *a, type *b) */
#define SWAP(t, a, b) {t tmp; tmp = *(a); *(a) = *(b); *(b) = tmp;}
