#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "graphics.h"
#include "internal.h"

Bunch *
allocbunch(ulong is)
{
	Bunch *b;

	b = _emalloc(sizeof *b);
	memset(b, 0, sizeof *b);
	b->itemsize = is;
	incref(b);
	return b;
}

ulong
bunchadd(Bunch *b, void *i)
{
	char *p;
	ulong idx;
	usize newlen;

	if(b->nitems % 16 == 0){
		newlen = (b->nitems + 16)*b->itemsize;
		b->items = _erealloc(b->items, newlen);
	}
	idx = b->nitems++;
	p = b->items;
	p += idx*b->itemsize;
	memmove(p, i, b->itemsize);
	return idx;
}

void *
bunchget(Bunch *b, ulong idx)
{
	char *p;

	if(idx >= b->nitems)
		return nil;

	p = b->items;
	p += idx*b->itemsize;
	return p;
}

Bunch *
refbunch(Bunch *b)
{
	incref(b);
	return b;
}

Bunch *
dupbunch(Bunch *b)
{
	Bunch *n;
	usize len;

	n = allocbunch(b->itemsize);
	len = b->nitems * b->itemsize;
	n->items = _erealloc(n->items, len);
	memmove(n->items, b->items, len);
	n->nitems = b->nitems;
	return n;
}

void
freebunch(Bunch *b)
{
	if(b == nil)
		return;

	if(decref(b) == 0){
		free(b->items);
		free(b);
	}
}
