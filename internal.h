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
void swapvertex(Vertex*, Vertex*);
void lerpvertex(Vertex*, Vertex*, Vertex*, double);
void berpvertex(Vertex*, Vertex*, Vertex*, Vertex*, Point3);
void delvattrs(Vertex*);
void fprintvattrs(int, Vertex*);

/* clip */
int clipprimitive(Primitive*, Primitive*);
int rectclipline(Rectangle, Point*, Point*, Vertex*, Vertex*);

/* util */
int min(int, int);
int max(int, int);
void swapi(int*, int*);
void swappt(Point*, Point*);
void memsetf(void*, float, usize);
void memsetl(void*, ulong, usize);

/* nanosec */
uvlong nanosec(void);

/* ulong getpixel(Framebuf *fb, Point p) */
#define getpixel(fb, p) (((fb)->cb)[Dx((fb)->r)*(p).y + (p).x])
/* void putpixel(Framebuf *fb, Point p, ulong c) */
#define putpixel(fb, p, c) (((fb)->cb)[Dx((fb)->r)*(p).y + (p).x] = (c))
