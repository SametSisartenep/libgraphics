typedef struct Polygon Polygon;
typedef struct Tilerparam Tilerparam;
typedef struct Rasterparam Rasterparam;
typedef struct Rastertask Rastertask;

struct Polygon
{
	Vertex *v;
	ulong n;
	ulong cap;
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
	Primitive p;
};

/* alloc */
void *emalloc(ulong);
void *erealloc(void*, ulong);
Memimage *eallocmemimage(Rectangle, ulong);

/* fb */
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

/* ulong getpixel(Framebuf *fb, Point p) */
#define getpixel(fb, p) (((fb)->cb)[Dx((fb)->r)*(p).y + (p).x])
/* void putpixel(Framebuf *fb, Point p, ulong c) */
#define putpixel(fb, p, c) (((fb)->cb)[Dx((fb)->r)*(p).y + (p).x] = (c))

/* void SWAP(type t, type *a, type *b) */
#define SWAP(t, a, b) {t tmp; tmp = *(a); *(a) = *(b); *(b) = tmp;}
