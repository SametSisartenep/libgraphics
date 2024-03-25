#define HZ2MS(hz)	(1000/(hz))

typedef enum {
	ORTHOGRAPHIC,
	PERSPECTIVE
} Projection;

enum {
	LIGHT_POINT,
	LIGHT_DIRECTIONAL,
	LIGHT_SPOT,
};

enum {
	VAPoint,
	VANumber,
};

typedef struct Color Color;
typedef struct Vertexattr Vertexattr;
typedef struct Vertex Vertex;
typedef struct LightSource LightSource;
typedef struct Material Material;
typedef struct Model Model;
typedef struct Entity Entity;
typedef struct Scene Scene;
typedef struct VSparams VSparams;
typedef struct FSparams FSparams;
typedef struct SUparams SUparams;
typedef struct Shadertab Shadertab;
typedef struct Renderer Renderer;
typedef struct Renderjob Renderjob;
typedef struct Framebuf Framebuf;
typedef struct Framebufctl Framebufctl;
typedef struct Viewport Viewport;
typedef struct Camera Camera;

struct Color
{
	double r, g, b, a;
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
	OBJMaterial *mtl;
	/* TODO it'd be neat to use a dynamic hash table instead */
	Vertexattr *attrs;	/* attributes (aka varyings) */
	ulong nattrs;
};

typedef Vertex Triangle[3];

struct LightSource
{
	Point3 p;
	Color c;
	int type;
};

struct Material
{
	Color ambient;
	Color diffuse;
	Color specular;
	double shininess;
};

struct Model
{
	OBJ *obj;
	Memimage *tex;		/* texture map */
	Memimage *nor;		/* normals map */
	Material *materials;
	ulong nmaterials;

	/* cache of renderable elems */
	OBJElem **elems;
	ulong nelems;
};

struct Entity
{
	RFrame3;
	Model *mdl;
	Entity *prev, *next;
};

struct Scene
{
	char *name;
	Entity ents;
	ulong nents;

	void (*addent)(Scene*, Entity*);
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
};

/* shader unit params */
struct SUparams
{
	Framebuf *fb;
	int id;
	Memimage *frag;
	Channel *donec;
	Renderjob *job;

	Entity *entity;

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

struct Renderjob
{
	Framebuf *fb;
	Scene *scene;
	Shadertab *shaders;
	Channel *donec;

	ulong nrem;		/* remaining entities to process */
	ulong lastid;
	uvlong time0;

	Renderjob *next;
};

struct Framebuf
{
	Memimage *cb;	/* color buffer */
	double *zbuf;	/* z/depth buffer */
	Lock zbuflk;
	Rectangle r;
};

struct Framebufctl
{
	Framebuf *fb[2];	/* double buffering */
	uint idx;		/* front buffer index */
	Lock swplk;

	void (*draw)(Framebufctl*, Image*);
	void (*memdraw)(Framebufctl*, Memimage*);
	void (*swap)(Framebufctl*);
	void (*reset)(Framebufctl*);
};

struct Viewport
{
	RFrame;
	Framebufctl *fbctl;

	void (*draw)(Viewport*, Image*);
	void (*memdraw)(Viewport*, Memimage*);
};

struct Camera
{
	RFrame3;		/* VCS */
	Viewport *vp;
	Scene *s;
	Renderer *rctl;
	double fov;		/* vertical FOV */
	struct {
		double n, f;	/* near and far clipping planes */
	} clip;
	Matrix3 proj;		/* VCS to NDC xform */
	Projection projtype;

	struct {
		uvlong min, avg, max, acc, n, v;
		uvlong nframes;
	} stats;
};

/* camera */
void configcamera(Camera*, Viewport*, double, double, double, Projection);
void placecamera(Camera*, Point3, Point3, Point3);
void aimcamera(Camera*, Point3);
void reloadcamera(Camera*);
void shootcamera(Camera*, Shadertab*);

/* viewport */
Viewport *mkviewport(Rectangle);
void rmviewport(Viewport*);

/* render */
Renderer *initgraphics(void);
Point3 model2world(Entity*, Point3);
Point3 world2vcs(Camera*, Point3);
Point3 vcs2clip(Camera*, Point3);
Point3 world2clip(Camera*, Point3);
void perspective(Matrix3, double, double, double, double);
void orthographic(Matrix3, double, double, double, double, double, double);

/* scene */
int refreshmodel(Model*);
Model *newmodel(void);
void delmodel(Model*);
Entity *newentity(Model*);
void delentity(Entity*);
Scene *newscene(char*);
void delscene(Scene*);

/* vertex */
void addvattr(Vertex*, char*, int, void*);
Vertexattr *getvattr(Vertex*, char*);

/* texture */
Color neartexsampler(Memimage*, Point2);
Color bilitexsampler(Memimage*, Point2);
Color texture(Memimage*, Point2, Color(*)(Memimage*, Point2));

/* util */
double fmin(double, double);
double fmax(double, double);
Memimage *rgb(ulong);

/* shadeop */
double sign(double);
double step(double, double);
double smoothstep(double, double, double);

extern Rectangle UR;	/* unit rectangle */
