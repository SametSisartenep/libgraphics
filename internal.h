typedef struct Deco Deco;
struct Deco
{
	int pfd[2];
	int infd;
	char *prog;
};

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
void shade(Framebuf *fb, OBJ *model, Memimage *modeltex, Shader *s, ulong nprocs);

/* util */
int min(int, int);
int max(int, int);
void memsetd(double*, double, usize);

/* nanosec */
uvlong nanosec(void);
