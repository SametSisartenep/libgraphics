#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "libobj/obj.h"
#include "graphics.h"
#include "internal.h"

static void
_turboproc(void *arg)
{
	Turbopool *p;
	Turbotask *t;

	threadsetname("turboproc");

	p = arg;

	while((t = recvp(p->subq)) != nil){
		t->fn(t->arg);
		free(t);
	}
}

Turbopool *
mkturbopool(ulong nprocs)
{
	Turbopool *p;

	p = emalloc(sizeof *p);
	memset(p, 0, sizeof *p);
	p->nprocs = nprocs;
	p->subq = chancreate(sizeof(void*), nprocs);
	while(nprocs--)
		proccreate(_turboproc, p, mainstacksize);
	return p;
}

void
turbopoolexec(Turbopool *p, void (*fn)(void*), void *arg)
{
	Turbotask *t;

	t = emalloc(sizeof *t);
	t->fn = fn;
	t->arg = arg;

	sendp(p->subq, t);
}

void
rmturbopool(Turbopool *p)
{
	while(p->nprocs--)
		sendp(p->subq, nil);
	chanfree(p->subq);
	free(p);
}
