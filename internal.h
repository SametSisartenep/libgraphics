typedef struct Tilerparam Tilerparam;
typedef struct Rastertask Rastertask;

struct Rastertask
{
	SUparams *params;
	Rectangle wr;		/* working rect */
	Triangle t;
};

struct Tilerparam
{
	Channel *paramsc;
	Channel **tasksc;	/* Channel*[nproc] */
	Rectangle *wr;		/* Rectangle[nproc] */
	ulong nproc;
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

/* util */
int min(int, int);
int max(int, int);
void memsetd(double*, double, usize);

/* nanosec */
uvlong nanosec(void);
