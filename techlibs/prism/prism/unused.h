#ifndef __unused
#if defined(__GNUC__)
#define __unused __attribute__ ((__unused__))
#else
#define __unused
#endif
#endif
