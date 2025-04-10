#include <u.h>
#include <libc.h>

void
_memsetl(void *dp, ulong v, usize len)
{
	ulong *p;

	p = dp;
	while(len--)
		*p++ = v;
}
