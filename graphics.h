typedef enum {
	Portho,		/* orthographic */
	Ppersp		/* perspective */
} Projection;

typedef struct Color Color;
typedef struct Vertex Vertex;
typedef struct Framebuffer Framebuffer;
typedef struct Viewport Viewport;
typedef struct Camera Camera;
typedef struct Triangle Triangle;

struct Color
{
	double r, g, b;
};

struct Vertex
{
	Point3 p;	/* position */
	Point3 n;	/* surface normal */
	Color c;	/* shading color */
	Point2 uv;	/* texture coordinate */
};

struct Framebuffer
{
	Rectangle r;	/* frame geometry */
	int bpp;	/* bytes per pixel */
	uchar *color;	/* pixel color buffer */
	float *depth;	/* pixel depth buffer */
};

struct Viewport
{
	RFrame;
	Framebuffer;
};

struct Camera
{
	RFrame3;		/* VCS */
	Image *viewport;
	double fov;		/* vertical FOV */
	struct {
		double n, f;	/* near and far clipping planes */
	} clip;
	Matrix3 proj;		/* VCS to NDC xform */
	Projection ptype;
};

struct Triangle
{
	Point p0, p1, p2;
};

/* Camera */
void configcamera(Camera*, Image*, double, double, double, Projection);
void placecamera(Camera*, Point3, Point3, Point3);
void aimcamera(Camera*, Point3);
void reloadcamera(Camera*);

/* rendering */
#define FPS	(60)		/* frame rate */
#define MS2FR	(1e3/FPS)	/* ms per frame */
Point3 world2vcs(Camera*, Point3);
Point3 vcs2ndc(Camera*, Point3);
Point3 world2ndc(Camera*, Point3);
int isclipping(Point3);
Point toviewport(Camera*, Point3);
Point2 fromviewport(Camera*, Point);
void perspective(Matrix3, double, double, double, double);
void orthographic(Matrix3, double, double, double, double, double, double);
/* temporary debug helpers */
void line3(Camera*, Point3, Point3, int, int, Image*);
Point string3(Camera*, Point3, Image*, Font*, char*);

/* triangle */
Triangle Trian(int, int, int, int, int, int);
Triangle Trianpt(Point, Point, Point);
void triangle(Image*, Triangle, int, Image*, Point);
void filltriangle(Image*, Triangle, Image*, Point);
