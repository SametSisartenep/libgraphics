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
	return a;
}

usize
itemarrayadd(ItemArray *a, void *i)
{
	char *p;
	usize idx;

	idx = a->nitems;
	a->items = _erealloc(a->items, ++a->nitems * a->itemsize);
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
copyitemarray(ItemArray *d, ItemArray *s)
{
	usize len;

	assert(d->itemsize == s->itemsize);
	len = s->nitems * s->itemsize;

	free(d->items);
	d->items = _emalloc(len);
	d->nitems = s->nitems;
	memmove(d->items, s->items, len);
	return d->nitems;
}

ItemArray *
dupitemarray(ItemArray *a)
{
	ItemArray *na;

	na = mkitemarray(a->itemsize);
	copyitemarray(na, a);
	return na;
}

void
rmitemarray(ItemArray *a)
{
	free(a->items);
	free(a);
}
