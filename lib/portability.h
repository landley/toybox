
// Humor glibc to get dprintf, then #define it to something more portable.
#define _GNU_SOURCE
#include <stdio.h>
#define fdprintf(...) dprintf(__VA_ARGS__)

#include <endian.h>

#if __BYTE_ORDER == __BIG_ENDIAN
#define IS_BIG_ENDIAN 1
#define IS_LITTLE_ENDIAN 0
#define SWAP_BE16(x) (x)
#define SWAP_BE32(x) (x)
#define SWAP_BE64(x) (x)
#define SWAP_LE16(x) bswap_16(x)
#define SWAP_LE32(x) bswap_32(x)
#define SWAP_LE64(x) bswap_64(x)
#else
#define IS_LITTLE_ENDIAN 1
#define IS_BIG_ENDIAN 0
#define SWAP_BE16(x) bswap_16(x)
#define SWAP_BE32(x) bswap_32(x)
#define SWAP_BE64(x) bswap_64(x)
#define SWAP_LE16(x) (x)
#define SWAP_LE32(x) (x)
#define SWAP_LE64(x) (x)
#endif
