#include <u.h>
#include <libc.h>
#include <tos.h>

/*
 * This code is a mixture of cpuid(1) and the nanosec() found in vmx,
 * in order to force the use of nsec(2) in case we are running in a
 * virtualized environment where the clock is mis-bhyve-ing.
 */

typedef struct Res {
	ulong ax, bx, cx, dx;
} Res;

static uchar _cpuid[] = {
	0x5E,			/* POP SI (PC) */
	0x5D,			/* POP BP (Res&) */
	0x58,			/* POP AX */
	0x59,			/* POP CX */

	0x51,			/* PUSH CX */
	0x50,			/* PUSH AX */
	0x55,			/* PUSH BP */
	0x56,			/* PUSH SI */

	0x31, 0xDB,		/* XOR BX, BX */
	0x31, 0xD2,		/* XOR DX, DX */

	0x0F, 0xA2,		/* CPUID */

	0x89, 0x45, 0x00,	/* MOV AX, 0(BP) */
	0x89, 0x5d, 0x04,	/* MOV BX, 4(BP) */
	0x89, 0x4d, 0x08,	/* MOV CX, 8(BP) */
	0x89, 0x55, 0x0C,	/* MOV DX, 12(BP) */
	0xC3,			/* RET */
};

static Res (*cpuid)(ulong ax, ulong cx) = (Res(*)(ulong, ulong)) _cpuid;

/*
 * nsec() is wallclock and can be adjusted by timesync
 * so need to use cycles() instead, but fall back to
 * nsec() in case we can't
 */
uvlong
nanosec(void)
{
	static uvlong fasthz, xstart;
	char buf[13], path[128];
	ulong w;
	uvlong x, div;
	int fd;
	Res r;

	if(fasthz == ~0ULL)
		return nsec() - xstart;

	if(fasthz == 0){
		/* first long in a.out header */
		snprint(path, sizeof path, "/proc/%d/text", getpid());
		fd = open(path, OREAD);
		if(fd < 0)
			goto Wallclock;
		if(read(fd, buf, 4) != 4){
			close(fd);
			goto Wallclock;
		}
		close(fd);

		w = ((ulong *) buf)[0];

		switch(w){
		default:
			goto Wallclock;
		case 0x978a0000:	/* amd64 */
			/* patch out POP BP -> POP AX */
			_cpuid[1] = 0x58;
		case 0xeb010000:	/* 386 */
			break;
		}
		segflush(_cpuid, sizeof(_cpuid));

		r = cpuid(0x40000000, 0);
		((ulong *) buf)[0] = r.bx;
		((ulong *) buf)[1] = r.cx;
		((ulong *) buf)[2] = r.dx;
		buf[12] = 0;

		if(strstr(buf, "bhyve") != nil)
			goto Wallclock;

		if(_tos->cyclefreq){
			fasthz = _tos->cyclefreq;
			cycles(&xstart);
		} else {
Wallclock:
			fasthz = ~0ULL;
			xstart = nsec();
		}
		return 0;
	}
	cycles(&x);
	x -= xstart;

	/* this is ugly */
	for(div = 1000000000ULL; x < 0x1999999999999999ULL && div > 1 ; div /= 10ULL, x *= 10ULL);

	return x / (fasthz / div);
}
