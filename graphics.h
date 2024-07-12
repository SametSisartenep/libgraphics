#define HZ2MS(hz)	(1000/(hz))

typedef enum {
	ORTHOGRAPHIC,
	PERSPECTIVE,
} Projection;

enum {
	PPoint,
	PLine,
	PTriangle,
};

enum {
	LIGHT_POINT,
	LIGHT_DIRECTIONAL,
	LIGHT_SPOT,
};

enum {
	RAWTexture,	/* unmanaged */
	sRGBTexture,
};

enum {
	VAPoint,
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
typedef struct FSparams FSparams;
typedef struct SUparams SUparams;
typedef struct Shadertab Shadertab;
typedef struct Renderer Renderer;
typedef struct Rendertime Rendertime;
typedef struct Renderjob Renderjob;
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
	Cubemap *skybox;

	void (*addent)(Scene*, Entity*);
	void (*delent)(Scene*, Entity*);
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

struct Framebuf
{
	ulong *cb;	/* color buffer */
	double *zb;	/* z/depth buffer */
	ulong *nb;	/* normals buffer (DBG only) */
	Rectangle r;
};

struct Framebufctl
{
	QLock;
	Framebuf *fb[2];	/* double buffering */
	uint idx;		/* front buffer index */

	void (*draw)(Framebufctl*, Image*);
	void (*memdraw)(Framebufctl*, Memimage*);
	void (*drawnormals)(Framebufctl*, Image*);
	void (*swap)(Framebufctl*);
	void (*reset)(Framebufctl*);
	Framebuf *(*getfb)(Framebufctl*);
	Framebuf *(*getbb)(Framebufctl*);
};

struct Viewport
{
	RFrame;
	Framebufctl *fbctl;

	void (*draw)(Viewport*, Image*);
	void (*memdraw)(Viewport*, Memimage*);
	Framebuf *(*getfb)(Viewport*);
};

struct Camera
{
	RFrame3;		/* VCS */
	Viewport *vp;
	Scene *scene;
	Renderer *rctl;
	double fov;		/* vertical FOV */
	struct {
		double n, f;	/* near and far clipping planes */
	} clip;
	Matrix3 proj;		/* VCS to clip space xform */
	Projection projtype;

	struct {
		uvlong min, avg, max, acc, n, v;
		uvlong nframes;
	} stats;

	struct {
		Rendertime R[100], E[100], Tn[100], Rn[100];
		int cur;
	} times;
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

/* scene */
int loadobjmodel(Model*, OBJ*);
Model *newmodel(void);
void delmodel(Model*);
Entity *newentity(Model*);
void delentity(Entity*);
Scene *newscene(char*);
void delscene(Scene*);
void clearscene(Scene*);

/* vertex */
void addvattr(Vertex*, char*, int, void*);
Vertexattr *getvattr(Vertex*, char*);

/* texture */
Texture *alloctexture(int, Memimage*);
void freetexture(Texture*);
Color neartexsampler(Texture*, Point2);
Color bilitexsampler(Texture*, Point2);
Color sampletexture(Texture*, Point2, Color(*)(Texture*, Point2));
Cubemap *readcubemap(char*[6]);
void freecubemap(Cubemap*);
Color samplecubemap(Cubemap*, Point3, Color(*)(Texture*, Point2));

/* util */
double fmin(double, double);
double fmax(double, double);
Point2 modulapt2(Point2, Point2);
Point3 modulapt3(Point3, Point3);
Memimage *rgb(ulong);

/* color */
Color srgb2linear(Color);
Color linear2srgb(Color);

/* shadeop */
double sign(double);
double step(double, double);
double smoothstep(double, double, double);
Color getlightcolor(LightSource*, Point3);

extern Rectangle UR;	/* unit rectangle */
