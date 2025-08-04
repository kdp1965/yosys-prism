#pragma once

#define BITS_PER_LONG (sizeof(unsigned long) << 3)

static inline unsigned int bitops_cnt(unsigned long x)
{
#ifdef __GNUC__
	return __builtin_popcountl(x);
#else
	int n = 0;

	for (; x; x &= x - 1)
		++n;

	return n;
#endif
}

static inline unsigned int bitops_ctz(unsigned long x)
{
	if (x == 0)
		return BITS_PER_LONG;
#ifdef __GNUC__
	return __builtin_ctzl(x);
#else
	int n = 0;

#if BITS_PER_LONG == 64
	if ((x & 0xffffffff) == 0) n = n + 32, x >>= 32;
#endif
	if ((x & 0x0000ffff) == 0) n = n + 16, x >>= 16;
	if ((x & 0x000000ff) == 0) n = n +  8, x >>=  8;
	if ((x & 0x0000000f) == 0) n = n +  4, x >>=  4;
	if ((x & 0x00000003) == 0) n = n +  2, x >>=  2;
	if ((x & 0x00000001) == 0) n = n +  1;

	return n;
#endif
}
