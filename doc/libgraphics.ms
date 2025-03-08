.\" Figure management
.nr FI 0 1
.de FI
.ce
\fBFigure \\n+(FI\fR: \\$1
..

.TL
libgraphics: Design and Implementation
.DA
.AU
Rodrigo G. López
rgl@antares-labs.eu
.AB
.I Libgraphics
is a 3D computer graphics library for Plan 9.  It implements a fully
concurrent retained mode software renderer for polygon rasterization,
and supports vertex and pixel shaders written in C (not GPU ones, at
least for now[1]), a z-buffer, front- and back-face culling,
textures, skyboxes, and directional and punctual lights, among other
things.
.AE
.SH
Introduction
.LP
.QP
Write the intro last.
.NH
The scene
.PP
.P1
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
};
.P2
.PP
A
.I scene
is a container, represented as a graph, that hosts the entities that
make up the world, as well as properties of it such as lighting and the
skybox.
.PP
.KS
.PS
.ps 7
boxwid = 0.5
boxht = 0.2
linewid = 0.1
lineht = 0.2
movewid = 0.1
moveht = 0.2
ellipsewid = 0.5
ellipseht = 0.25
ellipse "Scene"
down; line from last ellipse.s; line
left; line
box "Light 0"
left; line dashed
box dashed "Light 1"
left; line dashed
box dashed "..."
lineht = 0.12
right; line from 3rd last box.e; down; line; line
left; line
box "Skybox"
lineht = 0.2
move to last ellipse.s; down; move; right; line
box "Entity 0"
right; line dashed
box dashed "Entity 1"
right; line dashed
box dashed "..."
down; line from 3rd last box.s; right; line
box "Model"
down; line from last box.s; right; line
box dotted "Mesh"
down; line from last box.s; right; line
box "Primitive"
down
lineht = 0.12
line from 2nd last line.s; line; right; line
box "Material"
reset
.ps 10
.PE
.FI "The scene graph."
.KE
.NH 2
Entities
.PP
.P1
struct Entity
{
	RFrame3;
	char *name;
	Model *mdl;

	Entity *prev, *next;
};
.P2
.PP
.I Entities
represent visible physical objects in the scene.  Each of these
entities has a frame of reference to locate and orient it in the
world, a unique name and a localized model that encodes its physical
properties.
.NH 2
Models
.PP
.P1
struct Model
{
	Primitive *prims;
	ulong nprims;
	Material *materials;
	ulong nmaterials;

	int (*addprim)(Model*, Primitive);
	int (*addmaterial)(Model*, Material);
};
.P2
.PP
A model contains the geometric and material properties of an
.I Entity .
The geometry is encoded as a list of primitives, which in turn refer
to any out of a list of materials that encode the visual parameters
that determine its appearance.
.NH 2
Meshes
.PP
Meshes are not implemented yet, but the idea is for them to provide a
hierarchy of primitives with which to apply optimizations (for
storage, visibility determination, maybe others.)
.NH 2
Primitives
.PP
.P1
struct Primitive
{
	int type;
	Vertex v[3];
	Material *mtl;
	Point3 tangent;
};
.P2
.PP
Primitives are geometric building blocks, namely points, lines and
triangles.  The tangent is used in triangles for warp-safe normal
mapping.
.NH 2
Materials
.PP
.P1
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
.P2
.PP
A material defines the optical characteristics of a surface.
.NH
Cameras
.PP
.P1
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
};
.P2
.NH
The renderer
.LP
The
.I renderer
is the core of the library. It follows a
.B "retained mode"
model, which means that the user won't get a picture until the entire
scene has been rendered.  Thanks to this we can apply optimizations to
make better use of the pipeline, clear and swap the framebuffers,
and—in the future—run distributed rendering jobs, all without any
intervention; users only need to concern themselves with shooting and
“developing” a camera.
.LP
It's implemented as a tree of concurrent processes connected by buffered
.CW Channel s—as
seen in
.B "Figure 2" —,
spawned with a call to
.CW initgraphics ,
each representing a stage of the pipeline:
.KS
.PS
.ps 7
circlerad = 0.3
moveht = 0.1
arrowhead = 9
box "Renderjob"
arrow
R: circle "renderer"
arrow
E: circle "entityproc"
move
Tiler: [
	down
	T0: circle "tiler 1"
	move
	T1: circle "tiler 2"
	move
	Td: circle "…"
	move
	Tn: circle "tiler n"
]
move
Raster: [
	down
	R0: circle "rasterizer 1"
	move
	R1: circle "rasterizer 2"
	move
	Rd: circle "…"
	move
	Rn: circle "rasterizer n"
]
arrow from E to Tiler.T0 chop
arrow from E to Tiler.T1 chop
arrow from E to Tiler.Td chop
arrow from E to Tiler.Tn chop
arrow from Tiler.T0 to Raster.R0 chop
arrow from Tiler.T0 to Raster.R1 chop
arrow from Tiler.T0 to Raster.Rd chop
arrow from Tiler.T0 to Raster.Rn chop
arrow from Tiler.T1 to Raster.R0 chop
arrow from Tiler.T1 to Raster.R1 chop
arrow from Tiler.T1 to Raster.Rd chop
arrow from Tiler.T1 to Raster.Rn chop
.ps 10
.PE
.FI "The rendering graph for a \fB2n\fR processor machine."
.KE
.NH 2
renderer
.PP
The
.B renderer
process, the root of the tree, waits on a
.CW channel
for a
.CW Renderjob
sent by another user process, specifying a framebuffer, a scene, a
camera and a shader table.  It walks the scene and sends each
.CW Entity
individually to the
.B entityproc .
.NH 2
entityproc
.PP
The
.B entityproc
receives an entity and splits its geometry equitatively among the
.B tiler s,
sending a batch for each of them to process.
.NH 2
tilers
.PP
Next, each
.B tiler
gets to work on their subset of the geometry, potentially in
parallel—see
.B "Figure 3" .
They walk the list of primitives, then for each of them
apply the
.B "vertex shader"
to its vertices (which expects clip space coordinates in return),
perform frustum culling and clipping, back-face culling, and then
project them into the viewport to obtain their screen space
coordinates.  Following this step, they build a bounding box, used to
allocate each primitive into a rasterization bucket, or
.B tile ,
managed by one of the
.B rasterizer s;
as illustrated in
.B "Figure 4" .
If it spans multiple tiles, it will be copied and sent to each of
them.
.KS
.PS
.ps 7
Tiles: [
	boxht = 0.2
	boxwid = 1.25
	down
	T0: box dashed "tile 1"
	T1: box dashed "tile 2"
	Td: box dashed "…"
	Tn: box dashed "tile n"
]
box ht last [].ht+0.1 wid last [].wid+0.1 at last []
"Framebuf" rjust with .se at last [].nw - (0.1,0)
Raster: [
	moveht = 0.1
	down
	R0: circle "rasterizer 1"
	move
	R1: circle "rasterizer 2"
	move
	Rd: circle "…"
	move
	Rn: circle "rasterizer n"
] with .w at Tiles.e + (0.5,0)
line from Tiles.T0.e to Raster.R0.w
line from Tiles.T1.e to Raster.R1.w
line from Tiles.Td.e to Raster.Rd.w
line from Tiles.Tn.e to Raster.Rn.w
.ps 10
.PE
.FI "Per tile rasterizers."
.KE
.NH 2
rasterizers
.PP
Finally, the
.B rasterizers
receive the primitive in screen space, slice it to fit their tile, and
apply a rasterization routine based on its type.  For each of the
pixels, a
.B "depth test"
is performed, discarding fragments that are further away. Then a
.B "fragment shader"
is applied and the result written to the framebuffer after blending.
.QP
Depth testing and blending can be disabled by clearing the camera's
.CW RODepth
and
.CW ROBlend
bits from the
.CW rendopts
property, respectively.  An experimental A-buffer implementation is
also included for order-independent rendering of transparent
primitives (OIT).  If enabled, by setting the camera's
.CW ROAbuff
bit, fragments will be pushed to a depth-sorted stack, waiting
to be blended back-to-front and written to the framebuffer at the end
of the job.
.QE
.PP
.KS
.PS
.ps 7
Tiles: [
	boxht = 0.2
	boxwid = 1.25
	down
	T0: box dashed "1"
	T1: box dashed "2"
	Td: box dashed "…"
	Tn: box dashed "n"
]
line from last [].w + (0.1,-0.05) to last [].n - (-0.1,0.25)
line to last [].se - (0.3,-0.1)
line to 1st line
box ht last [].ht+0.1 wid last [].wid+0.1 at last []
"Framebuf" rjust with .se at last [].nw - (0.1,0)
Raster: [
	moveht = 0.1
	down
	R0: circle "rasterizer 1"
	move
	R1: circle "rasterizer 2"
	move
	Rd: circle "…"
	move
	Rn: circle "rasterizer n"
] with .w at Tiles.e + (0.5,0)
arrow from Tiles.T1.e to Raster.R1.w
arrow from Tiles.Td.e to Raster.Rd.w
arrow from Tiles.Tn.e to Raster.Rn.w
.ps 10
.PE
.FI "Raster task scheduling."
.KE
.NH
Frames of reference
.PP
Frames are right-handed throughout every stage of the pipeline, as
well as in the world.  A camera that's looking at an object has its
Z-axis basis
.CW bz ) (
pointing away from it.
.TE
.KS
.PS
.ps 7
RFrame: [
	pi = 3.1415926535
	deg = pi/180
	circle fill rad 0.01 at (0,0)
	"p" at last circle.c - (0.1,0)
	xa = -5*deg
	arrow from (0,0) to (cos(xa),sin(xa))
	"bx" at last arrow.end + (0.1,0)
	arrow from (0,0) to (0,1)
	"by" at last arrow.end - (0.1,0)
	za = -150*deg
	arrow from (0,0) to (cos(za)+0.1,sin(za)+0.1)
	"bz" at last arrow.end - (0.1,0)
]
.ps 10
.PE
.FI "Example right-handed rframe."
.KE
.NH
Viewports
.PP
A
.I viewport
is a sort of virtual framebuffer, a device that lets users configure
the way they visualize a framebuffer, which changes the resulting
.I image (6)
after a call to its
.CW draw
or
.CW memdraw
methods.  So far the only feature available is upscaling, which
includes user-defined filters for specific ratios, such as the family
of pixel art filters
.I Scale[234]x ,
used for 2x2, 3x3 and 4x4 scaling
.I [2] . respectively
Users control it with calls to the viewport's
.CW setscale
and
.CW setscalefilter
methods.
.KS
.PS
.ps 7
View: [
	boxwid = 3
	boxht = 2
	box with .nw at (-1,1)
	"Framebuf" at last box.s + (0,0.2)
	circle fill rad 0.01 at (-1,1)
	"p" at last circle.c - (0.1,0)
	arrow from (-1,1) to (-1,1) + (1,0)
	"bx" at last arrow.end + (0,0.1)
	arrow from (-1,1) to (-1,1) - (0,1)
	"by" at last arrow.end - (0.1,0)
]
.ps 10
.PE
.FI "Illustration of a 3:2 viewport."
.KE
.SH
References
.PP
.PP
.IP [1]
https://shithub.us/sirjofri/gpufs/HEAD/info.html
.IP [2]
https://www.scale2x.it/
.IP [3]
Thomas W. Crockett,
“Design Considerations for Parallel Graphics Libraries”,
.I
NASA Langley Research Center, Contract Nos. NAS1-18605 and NAS1-19480, June 1994
.IP [4]
Thomas W. Crockett,
“Parallel Rendering”,
.I
NASA Langley Research Center, Contract No. NAS1-19480, April 1995
.IP [5]
Thomas W. Crockett,
“Beyond the Renderer: Software Architecture for Parallel Graphics and Visualization”,
.I
NASA Langley Research Center, Contract No. NAS1-19480, December 1996
.IP [6]
Tomas Akenine-Möller et al,
“Real-Time Rendering”,
.I
4th edition, Taylor & Francis, CRC Press, 2018
.IP [7]
James F. Blinn, Martin E. Newell,
“Clipping Using Homogeneous Coordinates”,
.I
SIGGRAPH '78: Proceedings, August 1978, pp. 245-251
.IP [8]
“GPU Gems” series
.IP [9]
“Graphics Gems” series
.IP [10]
Ian Stephenson,
“Production Rendering: Design and Implementation”,
.I
Springer, 2005
.IP [11]
Paul S. Heckbert,
“Survey of Texture Mapping”,
.I
IEEE Computer Graphics and Applications, Nov. 1986, pp. 56-67
.IP [12]
Paul S. Heckbert,
“Fundamentals of Texture Mapping and Image Warping”,
.I
University of California, Berkeley, Technical Report No. UCB/CSD-89-516, June 1989
.IP [13]
Robert L. Cook, Loren Carpenter, Edwin Catmull
“The REYES Image Rendering Architecture”,
.I
ACM Transactions on Computer Graphics, Vol. 21, No. 4, July 1987
.IP [14]
Bruce J. Lindbloom,
“Accurate Color Reproduction for Computer Graphics Applications”,
.I
ACM Transactions on Computer Graphics, Vol. 23, No. 3, July 1989
