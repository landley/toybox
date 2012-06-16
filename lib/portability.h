// The tendency of gcc to produce stupid warnings continues with
// warn_unused_result, which warns about things like ignoring the return code
// of nice(2) (which is completely useless since -1 is a legitimate return
// value on success and even the man page tells you to use errno instead).

// This makes it stop.

#undef _FORTIFY_SOURCE

#define _FILE_OFFSET_BITS 64

// This isn't in the spec, but it's how we determine what we're using.

#include <features.h>

#ifdef __GLIBC__
// An SUSv4 function that glibc refuses to #define without crazy #defines,
// see http://pubs.opengroup.org/onlinepubs/9699919799/functions/strptime.html
#include <time.h>
char *strptime(const char *buf, const char *format, struct tm *tm);
// Another one. "Function prototypes shall be provided." but aren't.
// http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/unistd.h.html
char *crypt(const char *key, const char *salt);
#endif

#ifdef __GNUC__
#define noreturn	__attribute__((noreturn))
#else
#define noreturn
#endif

#ifndef __APPLE__
#include <byteswap.h>
#include <endian.h>

#if __BYTE_ORDER == __BIG_ENDIAN
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

int clearenv(void);
#else

#ifdef __BIG_ENDIAN__
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

#endif

#if IS_BIG_ENDIAN
#define IS_LITTLE_ENDIAN 0
#define SWAP_BE16(x) (x)
#define SWAP_BE32(x) (x)
#define SWAP_BE64(x) (x)
#define SWAP_LE16(x) bswap_16(x)
#define SWAP_LE32(x) bswap_32(x)
#define SWAP_LE64(x) bswap_64(x)
#else
#define IS_LITTLE_ENDIAN 1
#define SWAP_BE16(x) bswap_16(x)
#define SWAP_BE32(x) bswap_32(x)
#define SWAP_BE64(x) bswap_64(x)
#define SWAP_LE16(x) (x)
#define SWAP_LE32(x) (x)
#define SWAP_LE64(x) (x)
#endif

// Some versions of gcc produce spurious "may be uninitialized" warnings in
// cases where it provably can't happen.  Unfortunately, although this warning
// is calculated and produced separately from the "is definitely used
// uninitialized" warnings, there's no way to turn off the broken spurious "may
// be" warnings without also turning off the non-broken "is" warnings.

#if CFG_TOYBOX_DEBUG
#define GCC_BUG =0
#else
#define GCC_BUG
#endif

#if defined(__APPLE__) || defined(__ANDROID__)
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#endif
