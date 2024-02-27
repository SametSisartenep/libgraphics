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

typedef struct Color Color;
typedef struct Vertex Vertex;
typedef struct LightSource LightSource;
typedef struct Material Material;
typedef struct Model Model;
typedef struct Entity Entity;
typedef struct Scene Scene;
typedef struct VSparams VSparams;
typedef struct FSparams FSparams;
typedef struct SUparams SUparams;
typedef struct Shader Shader;
typedef struct Framebuf Framebuf;
typedef struct Framebufctl Framebufctl;
typedef struct Viewport Viewport;
typedef struct Camera Camera;

struct Color
{
	double r, g, b, a;
};

struct Vertex
{
	Point3 p;	/* position */
	Point3 n;	/* surface normal */
	Color c;	/* shading color */
	Point2 uv;	/* texture coordinate */

	/* TODO these attributes should be replaced by a hash table */
	double intensity;
	Point3 pos;
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
	Memimage *frag;
	Point p;
	Point3 bc;
	uchar *cbuf;
};

/* shader unit params */
struct SUparams
{
	Framebuf *fb;
	int id;
	Channel *donec;

	/* TODO replace with a Scene */
	Entity *entity;

	double var_intensity;
	Point3 var_normal;
	Point3 var_pos;

	uvlong uni_time;

	Point3 (*vshader)(VSparams*);
	Memimage *(*fshader)(FSparams*);
};

struct Shader
{
	char *name;
	Point3 (*vshader)(VSparams*);		/* vertex shader */
	Memimage *(*fshader)(FSparams*);	/* fragment shader */
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
	double fov;		/* vertical FOV */
	struct {
		double n, f;	/* near and far clipping planes */
	} clip;
	Matrix3 proj;		/* VCS to NDC xform */
	Projection projtype;

	struct {
		uvlong min, avg, max, acc, n, v;
	} stats;
};

/* camera */
void configcamera(Camera*, Viewport*, double, double, double, Projection);
void placecamera(Camera*, Point3, Point3, Point3);
void aimcamera(Camera*, Point3);
void reloadcamera(Camera*);
void shootcamera(Camera*, Shader*);

/* viewport */
Viewport *mkviewport(Rectangle);
void rmviewport(Viewport*);

/* render */
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

/* util */
double fmin(double, double);
double fmax(double, double);
Memimage *rgb(ulong);

/* shadeop */
double step(double, double);
double smoothstep(double, double, double);

extern Rectangle UR;	/* unit rectangle */
