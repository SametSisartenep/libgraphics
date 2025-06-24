- [ ] Scene description format
- [ ] Make a better Viewport interface
- [ ] Make the camera just another Entity (?)
- [ ] Implement shadows (hard, soft, CSM?)
- [ ] Implement mip-mapping (read about pixel shader derivatives)
	- I added gradients for incremental rasterization, could they be used for this?
- [ ] Try to compress the raster before doing a loadimage(2)
- [ ] Avoid writing the same texture multiple times under different names in exportmodel(2)
- [ ] Add wireframe rendering by a reasonable interface and method
- [ ] Find out why the A-buffer takes so much memory (enough to run OOM on a 32GB term!)
- [ ] Review the idea of using indexed properties for the vertices
	- This has become a more pressing matter with 3dee/toobj (the
	  Model to OBJ conversion tool) because its quite inefficient
	  and generates very large .obj files with unnecessary
	  redundancy: e.g., with a box, where instead of storing one
	  normal per every face, we store the same normal on the six
	  vertices (three per triangle) that make up the face.
- [+] Create an internal Vertex type
	- I want to get rid of the Vertex.(Vertexattrs|mtl|tangent)
	  properties, as they are only used during rasterization.
	  This will affect the Primitive as well, so it will probably
	  require an internal Primitive type.
- [ ] See if prims can be ordered front-to-back before rasterizing (quick Z-buffer discard)
	- It might be better to add it as a Camera.rendopts flag, for
	  transparency rendering without the A-buffer.
- [ ] Implement decals
