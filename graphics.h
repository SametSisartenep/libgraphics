#define HZ2MS(hz)	(1000/(hz))
#define HZ2NS(hz)	(1000000000ULL/(hz))
#define min(a, b)	((a)<(b)?(a):(b))
#define max(a, b)	((a)>(b)?(a):(b))

typedef enum {
	ORTHOGRAPHIC,
	PERSPECTIVE,
} Projection;

enum {
	/* culling modes */
	CullNone,
	CullFront,
	CullBack,

	/* render options */
	ROBlend	= 0x01,
	RODepth	= 0x02,
	ROAbuff	= 0x04,

	/* primitive types */
	PPoint = 0,
	PLine,
	PTriangle,

	/* light types */
	LightPoint = 0,
	LightDirectional,
	LightSpot,

	/* raster formats */
	COLOR32 = 0,	/* RGBA32 */
	FLOAT32,	/* F32 */

	/* texture types */
	RAWTexture = 0,	/* unmanaged */
	sRGBTexture,

	/* upscaling filters */
	UFNone = 0,	/* nearest neighbour */
	UFScale2x,
	UFScale3x,
	UFScale4x,

	/* vertex attribute types */
	VAPoint = 0,
	VANumber,
};

typedef struct Color Color;
typedef struct Texture Texture;
typedef struct Cubemap Cubemap;
typedef struct Vertexattr Vertexattr;
typedef struct Vertexattrs Vertexattrs;
typedef struct Vertex Vertex;
typedef struct LightSource LightSource;
typedef struct Material Material;
typedef struct Primitive Primitive;
typedef struct Model Model;
typedef struct Entity Entity;
typedef struct Scene Scene;
typedef struct Shaderparams Shaderparams;
typedef struct SUparams SUparams;
typedef struct Shadertab Shadertab;
typedef struct Rendertime Rendertime;
typedef struct Renderer Renderer;
typedef struct Renderjob Renderjob;
typedef struct Fragment Fragment;
typedef struct Astk Astk;
typedef struct Abuf Abuf;
typedef struct Raster Raster;
typedef struct Framebuf Framebuf;
typedef struct Framebufctl Framebufctl;
typedef struct Viewport Viewport;
typedef struct Camera Camera;

struct Color
{
	double r, g, b, a;
};

struct Texture
{
	Memimage *image;
	int type;
	char *file;
};

struct Cubemap
{
	char *name;
	Texture *faces[6];
};

/*
 * a more general approach worth investigating.
 * it could be made to handle types other than double.
 *
 * examples:
 * 	double intens;
 * 	addvattr(v, "intensity", 1, &intens);
 *
 * 	Point3 p;
 * 	addvattr(v, "normal", 3, &p);
 *
 * 	Matrix3 m;
 * 	addvattr(v, "proj", 4*4, m);
 */
//struct Vertexattr
//{
//	char *id;
//	int type;
//	ulong len;
//	double val[];
//};

struct Vertexattr
{
	char *id;
	int type;
	union {
		Point3 p;
		double n;
	};
};

struct Vertexattrs
{
	Vertexattr *attrs;
	ulong nattrs;
};

struct Vertex
{
	Point3 p;		/* position */
	Point3 n;		/* surface normal */
	Color c;		/* shading color */
	Point2 uv;		/* texture coordinate */
	Material *mtl;
	Point3 tangent;
	Vertexattrs;		/* attributes (varyings) */
};

struct LightSource
{
	int type;
	Point3 p;
	Point3 dir;
	Color c;
	double cutoff;	/* distance */
	/* spotlights */
	double θu;	/* umbra angle. anything beyond is unlit */
	double θp;	/* penumbra angle. anything within is fully lit */

	LightSource *prev, *next;
};

struct Material
{
	char *name;
	Color ambient;
	Color diffuse;
	Color specular;
	double shininess;
	Texture *diffusemap;
	Texture *specularmap;
	Texture *normalmap;
};

struct Primitive
{
	int type;
	Vertex v[3];
	Material *mtl;
	Point3 tangent;		/* used for normal mapping */
};

struct Model
{
	Primitive *prims;
	ulong nprims;
	Material *materials;
	ulong nmaterials;

	int (*addprim)(Model*, Primitive);
	int (*addmaterial)(Model*, Material);
	Material *(*getmaterial)(Model*, char*);
};

struct Entity
{
	RFrame3;
	char *name;
	Model *mdl;

	Entity *prev, *next;
};

struct Scene
{
	char *name;
	Entity ents;
	ulong nents;
	LightSource lights;
	ulong nlights;
	Cubemap *skybox;

	void (*addent)(Scene*, Entity*);
	void (*delent)(Scene*, Entity*);
	Entity *(*getent)(Scene*, char*);
	void (*addlight)(Scene*, LightSource*);
};

struct Shaderparams
{
	SUparams *su;
	Vertex *v;
	Point p;	/* fragment position (fshader-only) */
	uint idx;	/* vertex index (vshader-only) */

	Vertexattr *(*getuniform)(Shaderparams*, char*);
	Vertexattr *(*getattr)(Shaderparams*, char*);
	void (*setattr)(Shaderparams*, char*, int, void*);
	void (*toraster)(Shaderparams*, char*, void*);
};

/* shader unit params */
struct SUparams
{
	Framebuf *fb;
	Shadertab *stab;
	Renderjob *job;
	Camera *camera;
	Entity *entity;
	Primitive *eb, *ee;
};

struct Shadertab
{
	char *name;
	Point3 (*vs)(Shaderparams*);	/* vertex shader */
	Color (*fs)(Shaderparams*);	/* fragment shader */
	Vertexattrs;			/* uniforms */
};

struct Rendertime
{
	uvlong t0, t1;
};

struct Renderer
{
	Channel *jobq;
	ulong nprocs;
	int doprof;	/* enable profiling */
};

struct Renderjob
{
	Ref;
	uvlong id;
	Renderer *rctl;
	Framebuf *fb;
	Camera *camera;
	Shadertab *shaders;
	Channel *donec;
	Rectangle *cliprects;	/* one per rasterizer */
	int ncliprects;

	struct {
		/* renderer, entityproc, tilers, rasterizers */
		Rendertime R, E, *Tn, *Rn;
	} times;

	Renderjob *next;
};

struct Fragment
{
	Color c;
	float z;
};

struct Astk
{
	Point p;
	Fragment *items;
	ulong nitems;
	ulong size;
	int active;
};

struct Abuf
{
	QLock;
	Astk *stk;	/* framebuffer fragment stacks */
	Astk **act;	/* active fragment stacks */
	ulong nact;
};

struct Raster
{
	Rectangle r;
	char *name;
	ulong chan;
	ulong *data;

	Raster *next;
};

struct Framebuf
{
	Rectangle r;
	Rectangle clipr;
	Raster *rasters;	/* [0] color, [1] depth, [n] user-defined */
	Abuf abuf;		/* A-buffer */

	void (*createraster)(Framebuf*, char*, ulong);
	Raster *(*fetchraster)(Framebuf*, char*);
};

struct Framebufctl
{
	QLock;
	Framebuf *fb[2];	/* double buffering */
	uint idx;		/* front buffer index */
	uint upfilter;		/* upscaling filter */

	void (*draw)(Framebufctl*, Image*, char*, Point, Point);
	void (*memdraw)(Framebufctl*, Memimage*, char*, Point, Point);
	void (*swap)(Framebufctl*);
	void (*reset)(Framebufctl*);
	void (*createraster)(Framebufctl*, char*, ulong);
	Raster *(*fetchraster)(Framebufctl*, char*);
	Framebuf *(*getfb)(Framebufctl*);
	Framebuf *(*getbb)(Framebufctl*);
};

struct Viewport
{
	RFrame;
	Framebufctl *fbctl;
	Rectangle r;

	void (*draw)(Viewport*, Image*, char*);
	void (*memdraw)(Viewport*, Memimage*, char*);
	void (*setscale)(Viewport*, double, double);
	void (*setscalefilter)(Viewport*, int);
	Framebuf *(*getfb)(Viewport*);
	int (*getwidth)(Viewport*);
	int (*getheight)(Viewport*);
	void (*createraster)(Viewport*, char*, ulong);
	Raster *(*fetchraster)(Viewport*, char*);
};

struct Camera
{
	RFrame3;		/* VCS */
	Viewport *view;
	Scene *scene;
	Renderer *rctl;
	double fov;		/* vertical FOV */
	struct {
		double n, f;	/* near and far clipping planes */
	} clip;
	Matrix3 proj;		/* VCS to clip space xform */
	Projection projtype;
	int cullmode;
	uint rendopts;

	struct {
		uvlong min, avg, max, acc, n, v;
		uvlong nframes;
	} stats;
};

/* camera */
Camera *Camv(Viewport*, Renderer*, Projection, double, double, double);
Camera *Cam(Rectangle, Renderer*, Projection, double, double, double);
Camera *newcamera(void);
void delcamera(Camera*);
void reloadcamera(Camera*);
void configcamera(Camera*, Projection, double, double, double);
void placecamera(Camera*, Scene*, Point3, Point3, Point3);
void movecamera(Camera*, Point3);
void rotatecamera(Camera*, Point3, double);
void aimcamera(Camera*, Point3);
void shootcamera(Camera*, Shadertab*);

/* viewport */
Viewport *mkviewport(Rectangle);
void rmviewport(Viewport*);

/* render */
Renderer *initgraphics(void);
void setuniform(Shadertab*, char*, int, void*);

/* xform */
Point3 model2world(Entity*, Point3);
Point3 world2vcs(Camera*, Point3);
Point3 vcs2clip(Camera*, Point3);
Point3 world2clip(Camera*, Point3);
Point3 clip2ndc(Point3);
Point3 ndc2viewport(Framebuf*, Point3);
Point3 viewport2ndc(Framebuf*, Point3);
Point3 ndc2vcs(Camera*, Point3);
Point3 viewport2vcs(Camera*, Point3);
Point3 vcs2world(Camera*, Point3);
Point3 viewport2world(Camera*, Point3);
Point3 world2model(Entity*, Point3);
void perspective(Matrix3, double, double, double, double);
void orthographic(Matrix3, double, double, double, double, double, double);

/* marshal */
Model *readmodel(int);
usize writemodel(int, Model*, int);
int exportmodel(char*, Model*, int);

/* scene */
Material *newmaterial(char*);
void delmaterial(Material*);
Model *newmodel(void);
Model *dupmodel(Model*);
void delmodel(Model*);
LightSource *newpointlight(Point3, Color);
LightSource *newdireclight(Point3, Point3, Color);
LightSource *newspotlight(Point3, Point3, Color, double, double);
LightSource *duplight(LightSource*);
void dellight(LightSource*);
Entity *newentity(char*, Model*);
Entity *dupentity(Entity*);
void delentity(Entity*);
Scene *newscene(char*);
Scene *dupscene(Scene*);
void delscene(Scene*);
void clearscene(Scene*);

/* texture */
Texture *alloctexture(int, Memimage*);
Texture *duptexture(Texture*);
void freetexture(Texture*);
Color neartexsampler(Texture*, Point2);
Color bilitexsampler(Texture*, Point2);
Color sampletexture(Texture*, Point2, Color(*)(Texture*, Point2));
Cubemap *readcubemap(char*[6]);
Cubemap *dupcubemap(Cubemap*);
void freecubemap(Cubemap*);
Color samplecubemap(Cubemap*, Point3, Color(*)(Texture*, Point2));

/* util */
Point minpt(Point, Point);
Point maxpt(Point, Point);
Point2 modulapt2(Point2, Point2);
Point2 minpt2(Point2, Point2);
Point2 maxpt2(Point2, Point2);
int eqpt2(Point2, Point2);
Point3 modulapt3(Point3, Point3);
Point3 minpt3(Point3, Point3);
Point3 maxpt3(Point3, Point3);
int eqpt3(Point3, Point3);
Memimage *rgba(ulong);
Memimage *dupmemimage(Memimage*);

/* color */
ulong col2ul(Color);
Color ul2col(ulong);
int hasalpha(ulong);
Color srgb2linear(Color);
Color linear2srgb(Color);
Color aces(Color);
Color aces2(Color);

/* shadeop */
double sign(double);
double step(double, double);
double smoothstep(double, double, double);
Color getlightcolor(LightSource*, Point3, Point3);
Color getscenecolor(Scene*, Point3, Point3);

/* nanosec */
uvlong nanosec(void);

extern Rectangle UR;	/* unit rectangle */
extern Point2 ZP2;
extern Point3 ZP3;
