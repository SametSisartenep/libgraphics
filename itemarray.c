#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

ItemArray *
mkitemarray(usize is)
{
	ItemArray *a;

	a = _emalloc(sizeof *a);
	memset(a, 0, sizeof *a);
	a->itemsize = is;
	incref(a);
	return a;
}

usize
itemarrayadd(ItemArray *a, void *i)
{
	char *p;
	usize idx;

	idx = a->nitems;
	if(a->nitems++ % 16 == 0)
		a->items = _erealloc(a->items, (a->nitems + 15)*a->itemsize);
	p = a->items;
	p += idx*a->itemsize;
	memmove(p, i, a->itemsize);
	return idx;
}

void *
itemarrayget(ItemArray *a, usize idx)
{
	char *p;

	if(idx >= a->nitems)
		return nil;

	p = a->items;
	p += idx*a->itemsize;
	return p;
}

usize
copyitemarray(ItemArray *s, ItemArray *d)
{
	usize len;

	len = s->nitems * s->itemsize;
	free(d->items);
	d->items = _emalloc(len);
	d->nitems = s->nitems;
	d->itemsize = s->itemsize;
	memmove(d->items, s->items, len);
	return d->nitems;
}

/*
 * deferring rmitemarray() makes this operation idempotent.
 */
ItemArray *
dupitemarray(ItemArray *s, ItemArray **d)
{
	ItemArray *t;

	t = *d;
	*d = s;
	incref(*d);
	rmitemarray(t);
	return *d;
}

void
rmitemarray(ItemArray *a)
{
	if(decref(a) == 0){
		free(a->items);
		free(a);
	}
}
