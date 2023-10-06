#include	<assert.h>
#include	"dthdr.h"
#include	<limits.h>
#include	<string.h>

/* Hashing a string into an unsigned integer.
** The basic method is to continuingly accumulate bytes and multiply
** with some given prime. The length n of the string is added last.
** The recurrent equation is like this:
**	h[k] = (h[k-1] + bytes)*prime	for 0 <= k < n
**	h[n] = (h[n-1] + n)*prime
** The prime is chosen to have a good distribution of 1-bits so that
** the multiplication will distribute the bits in the accumulator well.
** The below code accumulates 2 bytes at a time for speed.
**
** Written by Kiem-Phong Vo (02/28/03)
*/

uint dtstrhash(uint h, void* args, int n)
{
	unsigned char *s = (unsigned char*) args;

	if(n <= 0)
	{	for(; *s != 0; s += s[1] ? 2 : 1)
			h = (h + ((unsigned)s[0] << 8u) + (unsigned)s[1]) * DT_PRIME;
		// assert(strlen(args) <= INT_MAX);
		n = (int)(s - (unsigned char*)args);
	}
	else
	{	unsigned char*	ends;
		for(ends = s+n-1; s < ends; s += 2)
			h = (h + ((unsigned)s[0] << 8u) + (unsigned)s[1]) * DT_PRIME;
		if(s <= ends)
			h = (h + ((unsigned)s[0] << 8u)) * DT_PRIME;
	}
	assert(n >= 0);
	return (h + (unsigned)n) * DT_PRIME;
}
