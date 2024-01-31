/* alloc */
void *emalloc(ulong);
void *erealloc(void*, ulong);
Memimage *eallocmemimage(Rectangle, ulong);

/* fb */
Framebuf *mkfb(Rectangle);
void rmfb(Framebuf*);
Framebufctl *mkfbctl(Rectangle);
void rmfbctl(Framebufctl*);

/* render */
void shade(Framebuf*, OBJ*, Memimage*, Shader*, ulong);

/* util */
int min(int, int);
int max(int, int);
void memsetd(double*, double, usize);

/* nanosec */
uvlong nanosec(void);
