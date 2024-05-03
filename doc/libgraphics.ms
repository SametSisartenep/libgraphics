.TL
libgraphics
.AU
Rodrigo G. López
rgl@antares-labs.eu
.AB
To be determined.
.AE
.SH
Rendering graph
.PP
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
	T0: circle "tiler 0"
	move
	T1: circle "tiler 1"
	move
	Td: circle "…"
	move
	Tn: circle "tiler n"
]
move
Raster: [
	down
	R0: circle "rasterizer 0"
	move
	R1: circle "rasterizer 1"
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
.B "Figure 1" :
For a
.B 2(n+1)
processor machine.
.SH
The scene
.PP
.PS
.ps 7
boxwid = 0.5
boxht = 0.2
linewid = 0.1
lineht = 0.2
box "Scene"
down; line from last box.s; right; line
box "Entity"
down; line from last box.s; right; line
box "Model"
down; line from last box.s; right; line
box "Mesh"
down; line from last box.s; right; line
box "Primitive"
down
line from 2nd last line.s; line; right; line
box "Material"
.ps 10
.PE
.SH
Tile-based rendering
.PP
.PS
.ps 7
Tiles: [
	boxht = 0.2
	boxwid = 1.25
	down
	T0: box dashed "tile 0"
	T1: box dashed "tile 1"
	Td: box dashed "…"
	Tn: box dashed "tile n"
]
box ht last [].ht+0.1 wid last [].wid+0.1 at last []
"Screen" rjust with .se at last [].nw - (0.1,0)
Raster: [
	moveht = 0.1
	down
	R0: circle "rasterizer 0"
	move
	R1: circle "rasterizer 1"
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
.B "Figure 2" :
Per tile rasterizers.
.PS
.ps 7
Tiles: [
	boxht = 0.2
	boxwid = 1.25
	down
	T0: box dashed "0"
	T1: box dashed "1"
	Td: box dashed "…"
	Tn: box dashed "n"
]
line from last [].w + (0.1,-0.05) to last [].n - (-0.1,0.25)
line to last [].se - (0.3,-0.1)
line to 1st line
box ht last [].ht+0.1 wid last [].wid+0.1 at last []
"Screen" rjust with .se at last [].nw - (0.1,0)
Raster: [
	moveht = 0.1
	down
	R0: circle "rasterizer 0"
	move
	R1: circle "rasterizer 1"
	move
	Rd: circle "…"
	move
	Rn: circle "rasterizer n"
] with .w at Tiles.e + (0.5,0)
arrow dashed from Tiles.T0.e to Raster.R0.w
arrow from Tiles.T1.e to Raster.R1.w
arrow from Tiles.Td.e to Raster.Rd.w
arrow from Tiles.Tn.e to Raster.Rn.w
.ps 10
.PE
.B "Figure 3" :
Raster task scheduling.
