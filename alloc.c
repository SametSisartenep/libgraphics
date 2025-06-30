#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

void *
_emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		sysfatal("malloc: %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

void *
_erealloc(void *p, ulong n)
{
	void *np;

	np = realloc(p, n);
	if(np == nil){
		if(n == 0)
			return nil;
		sysfatal("realloc: %r");
	}
	if(p == nil)
		setmalloctag(np, getcallerpc(&p));
	else
		setrealloctag(np, getcallerpc(&p));
	return np;
}

char *
_estrdup(char *s)
{
	char *ns;

	ns = strdup(s);
	if(ns == nil)
		sysfatal("strdup: %r");
	setmalloctag(ns, getcallerpc(&s));
	return ns;
}

char *
_equotestrdup(char *s)
{
	char *ns;

	ns = quotestrdup(s);
	if(ns == nil)
		sysfatal("quotestrdup: %r");
	setmalloctag(ns, getcallerpc(&s));
	return ns;
}

Memimage *
_eallocmemimage(Rectangle r, ulong chan)
{
	Memimage *i;

	i = allocmemimage(r, chan);
	if(i == nil)
		sysfatal("allocmemimage: %r");
	memfillcolor(i, DTransparent);
	return i;
}
