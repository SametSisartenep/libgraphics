#define HZ2MS(hz)	(1000/(hz))
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

	/* primitive types */
	PPoint = 0,
	PLine,
	PTriangle,

	/* light types */
	LightPoint = 0,
	LightDirectional,
	LightSpot,

	/* raster formats */
	COLOR32 = 0,
	FLOAT32,

	/* texture formats */
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
typedef struct Vertex Vertex;
typedef struct LightSource LightSource;
typedef struct Material Material;
typedef struct Primitive Primitive;
typedef struct Model Model;
typedef struct Entity Entity;
typedef struct Scene Scene;
typedef struct VSparams VSparams;
typedef struct FSoutput FSoutput;
typedef struct FSparams FSparams;
typedef struct SUparams SUparams;
typedef struct Shadertab Shadertab;
typedef struct Renderer Renderer;
typedef struct Rendertime Rendertime;
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

struct Vertex
{
	Point3 p;		/* position */
	Point3 n;		/* surface normal */
	Color c;		/* shading color */
	Point2 uv;		/* texture coordinate */
	Material *mtl;
	Point3 tangent;

	/* TODO it'd be neat to use a dynamic hash table instead */
	Vertexattr *attrs;	/* attributes (aka varyings) */
	ulong nattrs;
};

struct LightSource
{
	Point3 p;
	Point3 dir;
	Color c;
	int type;
	/* spotlights */
	double θu;	/* umbra angle. anything beyond is unlit */
	double θp;	/* penumbra angle. anything within is fully lit */
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

	Texture *tex;		/* texture map (TODO get rid of it, use materials) */

	int (*addprim)(Model*, Primitive);
	int (*addmaterial)(Model*, Material);
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
	Cubemap *skybox;

	void (*addent)(Scene*, Entity*);
	void (*delent)(Scene*, Entity*);
	Entity *(*getent)(Scene*, char*);
};

/* shader params */
struct VSparams
{
	SUparams *su;
	Vertex *v;
	uint idx;
};

struct FSparams
{
	SUparams *su;
	Point p;
	Vertex v;		/* only for the attributes (varyings) */

	void (*toraster)(FSparams*, char*, void*);
};

/* shader unit params */
struct SUparams
{
	Framebuf *fb;
	Renderjob *job;
	Camera *camera;
	Entity *entity;
	Primitive *eb, *ee;

	uvlong uni_time;

	Point3 (*vshader)(VSparams*);
	Color (*fshader)(FSparams*);
};

struct Shadertab
{
	char *name;
	Point3 (*vshader)(VSparams*);	/* vertex shader */
	Color (*fshader)(FSparams*);	/* fragment shader */
};

struct Renderer
{
	Channel *c;
};

struct Rendertime
{
	uvlong t0, t1;
};

struct Renderjob
{
	Ref;
	uvlong id;
	Framebuf *fb;
	Camera *camera;
	Scene *scene;
	Shadertab *shaders;
	Channel *donec;

	struct {
		Rendertime R, E, Tn, Rn;	/* renderer, entityproc, tilers, rasterizers */
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
	void (*reset)(Framebufctl*, ulong);
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
	ulong clearcolor;
	int cullmode;
	int enableblend;
	int enabledepth;
	int enableAbuff;

	struct {
		uvlong min, avg, max, acc, n, v;
		uvlong nframes;
	} stats;
	struct {
		Rendertime R[10], E[10], Tn[10], Rn[10];
		int last, cur;
	} times;
};

/* camera */
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

/* obj */
int loadobjmodel(Model*, OBJ*);
Model *readobjmodel(char*);

/* scene */
Model *newmodel(void);
Model *dupmodel(Model*);
void delmodel(Model*);
Entity *newentity(char*, Model*);
Entity *dupentity(Entity*);
void delentity(Entity*);
Scene *newscene(char*);
Scene *dupscene(Scene*);
void delscene(Scene*);
void clearscene(Scene*);

/* vertex */
void addvattr(Vertex*, char*, int, void*);
Vertexattr *getvattr(Vertex*, char*);

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
Point2 modulapt2(Point2, Point2);
Point3 modulapt3(Point3, Point3);
Memimage *rgb(ulong);
Memimage *dupmemimage(Memimage*);

/* color */
Color srgb2linear(Color);
Color linear2srgb(Color);
ulong rgba2xrgb(ulong);
Color aces(Color);
Color aces2(Color);

/* shadeop */
double sign(double);
double step(double, double);
double smoothstep(double, double, double);
Color getlightcolor(LightSource*, Point3);

extern Rectangle UR;	/* unit rectangle */
