/* md5sum.c - Calculate hashes md5, sha1, sha224, sha256, sha384, sha512.
 *
 * Copyright 2012, 2021 Rob Landley <rob@landley.net>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/md5sum.html
 * and http://www.ietf.org/rfc/rfc1321.txt
 * and http://www.ietf.org/rfc/rfc4634.txt
 *
 * They're combined this way to share infrastructure, and because md5sum is
 * a LSB standard command (but sha1sum and newer hashes are a good idea,
 * see http://valerieaurora.org/hash.html).
 *
 * We optionally use openssl (or equivalent) to access assembly optimized
 * versions of these functions, but provide a built-in version to reduce
 * required dependencies.
 *
 * coreutils supports --status but not -s, busybox supports -s but not --status

USE_MD5SUM(NEWTOY(md5sum, "bc(check)s(status)[!bc]", TOYFLAG_USR|TOYFLAG_BIN))
USE_SHA1SUM(NEWTOY(sha1sum, "bc(check)s(status)[!bc]", TOYFLAG_USR|TOYFLAG_BIN))
USE_SHA256SUM(NEWTOY(sha224sum, "bc(check)s(status)[!bc]", TOYFLAG_USR|TOYFLAG_BIN))
USE_SHA256SUM(NEWTOY(sha256sum, "bc(check)s(status)[!bc]", TOYFLAG_USR|TOYFLAG_BIN))
USE_SHA256SUM(NEWTOY(sha384sum, "bc(check)s(status)[!bc]", TOYFLAG_USR|TOYFLAG_BIN))
USE_SHA512SUM(NEWTOY(sha512sum, "bc(check)s(status)[!bc]", TOYFLAG_USR|TOYFLAG_BIN))

config MD5SUM
  bool "md5sum"
  default y
  help
    usage: md5sum [-bcs] [FILE]...

    Calculate md5 hash for each input file, reading from stdin if none.
    Output one hash (32 hex digits) for each input file, followed by filename.

    -b	Brief (hash only, no filename)
    -c	Check each line of each FILE is the same hash+filename we'd output
    -s	No output, exit status 0 if all hashes match, 1 otherwise

config SHA1SUM
  bool "sha1sum"
  default y
  help
    usage: sha?sum [-bcs] [FILE]...

    Calculate sha hash for each input file, reading from stdin if none. Output
    one hash (40 hex digits for sha1, 56 for sha224, 64 for sha256, 96 for sha384,
    and 128 for sha512) for each input file, followed by filename.

    -b	Brief (hash only, no filename)
    -c	Check each line of each FILE is the same hash+filename we'd output
    -s	No output, exit status 0 if all hashes match, 1 otherwise

config SHA224SUM
  bool "sha224sum"
  default y
  help
    See sha1sum

config SHA256SUM
  bool "sha256sum"
  default y
  help
    See sha1sum

config SHA384SUM
  bool "sha384sum"
  default y
  help
    See sha1sum

config SHA512SUM
  bool "sha512sum"
  default y
  help
    See sha1sum
*/

#define FORCE_FLAGS
#define FOR_md5sum
#include "toys.h"

#if CFG_TOYBOX_LIBCRYPTO
#include <openssl/md5.h>
#include <openssl/sha.h>
#else
typedef int SHA512_CTX;
#endif

GLOBALS(
  int sawline;

  enum hashmethods { MD5, SHA1, SHA224, SHA256, SHA384, SHA512 } hashmethod;

  uint32_t *rconsttable32;
  uint64_t *rconsttable64; // for sha384,sha512
  // Crypto variables blanked after summing
  union {
    uint32_t i32[8]; // for md5,sha1,sha224,sha256
    uint64_t i64[8]; // for sha384,sha512
  } state;
  uint64_t count; // the spec for sha384 and sha512
                  // uses a 128-bit number to count
		  // the amount of input bits. When
		  // using a 64-bit number, the
		  // maximum input data size is
		  // about 23 petabytes.
  union {
    char c[128]; // bytes, 1024 bits
    uint32_t i32[16]; // 512 bits for md5,sha1,sha224,sha256
    uint64_t i64[16]; // 1024 bits for sha384,sha512
  } buffer;
)

// Round constants. Static table for when we haven't got floating point support
#if ! CFG_TOYBOX_FLOAT
static const uint32_t md5nofloat[64] = {
  0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
  0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
  0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
  0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
  0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
  0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
  0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
  0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
  0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
  0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
  0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};
#else // TODO: move this below the sha512 definition
#define md5nofloat 0
static const uint8_t primegaps[79] = {
 1, 2, 2, 4, 2, 4, 2, 4, 6, 2,
 6, 4, 2, 4, 6, 6, 2, 6, 4, 2,
 6, 4, 6, 8, 4, 2, 4, 2, 4,14,
 4, 6, 2,10, 2, 6, 6, 4, 6, 6,
 2,10, 2, 4, 2,12,12, 4, 2, 4,
 6, 2,10, 6, 6, 6, 2, 6, 4, 2,
10,14, 4, 2, 4,14, 6,10, 2, 4,
 6, 8, 6, 6, 4, 6, 8, 4, 8 };
#endif
static const uint64_t sha512nofloat[80] = {
  0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f,
  0xe9b5dba58189dbbc, 0x3956c25bf348b538, 0x59f111f1b605d019,
  0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242,
  0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
  0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
  0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3,
  0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65, 0x2de92c6f592b0275,
  0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
  0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f,
  0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
  0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc,
  0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
  0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6,
  0x92722c851482353b, 0xa2bfe8a14cf10364, 0xa81a664bbc423001,
  0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
  0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
  0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99,
  0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb,
  0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc,
  0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
  0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915,
  0xc67178f2e372532b, 0xca273eceea26619c, 0xd186b8c721c0c207,
  0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba,
  0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
  0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
  0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a,
  0x5fcb6fab3ad6faec, 0x6c44198c4a475817
};
// sha1 needs only 4 round constant values, so prefer precomputed
static const uint32_t sha1rconsts[] = {
  0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
};

// bit rotations
#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
#define ror(value, bits) (((value) >> (bits)) | ((value) << (32 - (bits))))
#define ror64(value, bits) (((value) >> (bits)) | ((value) << (64 - (bits))))

// Mix next 64 bytes of data into md5 hash

static void md5_transform(void)
{
  uint32_t x[4], *b = (uint32_t *)TT.buffer.c;
  int i;

  memcpy(x, TT.state.i32, sizeof(x));

  for (i=0; i<64; i++) {
    uint32_t in, a, rot, temp;

    a = (-i)&3;
    if (i<16) {
      in = i;
      rot = 7+(5*(i&3));
      temp = x[(a+1)&3];
      temp = (temp & x[(a+2)&3]) | ((~temp) & x[(a+3)&3]);
    } else if (i<32) {
      in = (1+(5*i))&15;
      temp = (i&3)+1;
      rot = temp*5;
      if (temp&2) rot--;
      temp = x[(a+3)&3];
      temp = (x[(a+1)&3] & temp) | (x[(a+2)&3] & ~temp);
    } else if (i<48) {
      in = (5+(3*(i&15)))&15;
      rot = i&3;
      rot = 4+(5*rot)+((rot+1)&6);
      temp = x[(a+1)&3] ^ x[(a+2)&3] ^ x[(a+3)&3];
    } else {
      in = (7*(i&15))&15;
      rot = (i&3)+1;
      rot = (5*rot)+(((rot+2)&2)>>1);
      temp = x[(a+2)&3] ^ (x[(a+1)&3] | ~x[(a+3)&3]);
    }
    temp += x[a] + b[in] + TT.rconsttable32[i];
    x[a] = x[(a+1)&3] + ((temp<<rot) | (temp>>(32-rot)));
  }
  for (i=0; i<4; i++) TT.state.i32[i] += x[i];
}

// Mix next 64 bytes of data into sha1 hash.

static void sha1_transform(void)
{
  int i, j, k, count;
  uint32_t *block = TT.buffer.i32;
  uint32_t oldstate[5];
  uint32_t *rot[5], *temp;

  // Copy context->state.i32[] to working vars
  for (i=0; i<5; i++) {
    oldstate[i] = TT.state.i32[i];
    rot[i] = TT.state.i32 + i;
  }
  // 4 rounds of 20 operations each.
  for (i=count=0; i<4; i++) {
    for (j=0; j<20; j++) {
      uint32_t work;

      work = *rot[2] ^ *rot[3];
      if (!i) work = (work & *rot[1]) ^ *rot[3];
      else {
        if (i==2) work = ((*rot[1]|*rot[2])&*rot[3])|(*rot[1]&*rot[2]);
        else work ^= *rot[1];
      }

      if (!i && j<16)
        work += block[count] = (rol(block[count],24)&0xFF00FF00)
                             | (rol(block[count],8)&0x00FF00FF);
      else
        work += block[count&15] = rol(block[(count+13)&15]
              ^ block[(count+8)&15] ^ block[(count+2)&15] ^ block[count&15], 1);
      *rot[4] += work + rol(*rot[0],5) + sha1rconsts[i];
      *rot[1] = rol(*rot[1],30);

      // Rotate by one for next time.
      temp = rot[4];
      for (k=4; k; k--) rot[k] = rot[k-1];
      *rot = temp;
      count++;
    }
  }
  // Add the previous values of state.i32[]
  for (i=0; i<5; i++) TT.state.i32[i] += oldstate[i];
}

static void sha256_transform(void)
{
  int i;
  uint32_t block[64];
  uint32_t s0, s1, S0, S1, ch, maj, temp1, temp2;
  uint32_t rot[8]; // a,b,c,d,e,f,g,h

  /*
  printf("buffer.c[0 - 4] = %02hhX %02hhX %02hhX %02hhX %02hhX\n", TT.buffer.c[0], TT.buffer.c[1], TT.buffer.c[2], TT.buffer.c[3], TT.buffer.c[4]);
  printf("buffer.c[56 - 63] = %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n", \
    TT.buffer.c[56], TT.buffer.c[57], TT.buffer.c[58], TT.buffer.c[59], \
    TT.buffer.c[60], TT.buffer.c[61], TT.buffer.c[62], TT.buffer.c[63]);
  */
  for (i=0; i<16; i++) {
    block[i] = SWAP_BE32(TT.buffer.i32[i]);
  }
  // Extend the message schedule array beyond first 16 words
  for (i=16; i<64; i++) {
    s0 = ror(block[i-15], 7) ^ ror(block[i-15], 18) ^ (block[i-15] >> 3);
    s1 = ror(block[i-2], 17) ^ ror(block[i-2], 19) ^ (block[i-2] >> 10);
    block[i] = block[i-16] + s0 + block[i-7] + s1;
  }
  // Copy context->state.i32[] to working vars
  for (i=0; i<8; i++) {
    //TT.oldstate32[i] = TT.state.i32[i];
    rot[i] = TT.state.i32[i];
  }
  // 64 rounds
  for (i=0; i<64; i++) {
    S1 = ror(rot[4],6) ^ ror(rot[4],11) ^ ror(rot[4], 25);
    ch = (rot[4] & rot[5]) ^ ((~ rot[4]) & rot[6]);
    temp1 = rot[7] + S1 + ch + TT.rconsttable32[i] + block[i];
    S0 = ror(rot[0],2) ^ ror(rot[0],13) ^ ror(rot[0], 22);
    maj = (rot[0] & rot[1]) ^ (rot[0] & rot[2]) ^ (rot[1] & rot[2]);
    temp2 = S0 + maj;
    /* if (i < 2) {
      printf("begin round %d: rot[0] = %u  rot[1] = %u  rot[2] = %u\n", i, rot[0], rot[1], rot[2]);
      printf("  S1=%u ch=%u temp1=%u S0=%u maj=%u temp2=%u\n", S1,ch,temp1,S0,maj,temp2);
      printf("  rot[7]=%u K[i]=%u W[i]=%u TT.buffer.i[i]=%u\n", rot[7],TT.rconsttable32[i],block[i],TT.buffer.i[i]);
    } */
    rot[7] = rot[6];
    rot[6] = rot[5];
    rot[5] = rot[4];
    rot[4] = rot[3] + temp1;
    rot[3] = rot[2];
    rot[2] = rot[1];
    rot[1] = rot[0];
    rot[0] = temp1 + temp2;
  }
  //printf("%d rounds done: rot[0] = %u  rot[1] = %u  rot[2] = %u\n", i, rot[0], rot[1], rot[2]);

  // Add the previous values of state.i32[]
  for (i=0; i<8; i++) TT.state.i32[i] += rot[i];
  //printf("state.i32[0] = %u  state.i32[1] = %u  state.i32[2] = %u\n", TT.state.i32[0], TT.state.i32[1], TT.state.i32[2]);
}

static void sha224_transform(void)
{
  sha256_transform();
}

static void sha512_transform(void)
{
  int i;
  uint64_t block[80];
  uint64_t s0, s1, S0, S1, ch, maj, temp1, temp2;
  uint64_t rot[8]; // a,b,c,d,e,f,g,h

  /*
  printf("buffer.c[0 - 4] = %02hhX %02hhX %02hhX %02hhX %02hhX\n", TT.buffer.c[0], TT.buffer.c[1], TT.buffer.c[2], TT.buffer.c[3], TT.buffer.c[4]);
  printf("buffer.c[112 - 127] = %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n", \
    TT.buffer.c[112], TT.buffer.c[113], TT.buffer.c[114], TT.buffer.c[115], \
    TT.buffer.c[116], TT.buffer.c[117], TT.buffer.c[118], TT.buffer.c[119], \
    TT.buffer.c[120], TT.buffer.c[121], TT.buffer.c[122], TT.buffer.c[123], \
    TT.buffer.c[124], TT.buffer.c[125], TT.buffer.c[126], TT.buffer.c[127]);
  */
  for (i=0; i<16; i++) {
    block[i] = SWAP_BE64(TT.buffer.i64[i]);
  }
  // Extend the message schedule array beyond first 16 words
  for (i=16; i<80; i++) {
    s0 = ror64(block[i-15], 1) ^ ror64(block[i-15], 8) ^ (block[i-15] >> 7);
    s1 = ror64(block[i-2], 19) ^ ror64(block[i-2], 61) ^ (block[i-2] >> 6);
    block[i] = block[i-16] + s0 + block[i-7] + s1;
  }
  // Copy context->state.i64[] to working vars
  for (i=0; i<8; i++) {
    rot[i] = TT.state.i64[i];
  }
  // 80 rounds
  for (i=0; i<80; i++) {
    S1 = ror64(rot[4],14) ^ ror64(rot[4],18) ^ ror64(rot[4], 41);
    ch = (rot[4] & rot[5]) ^ ((~ rot[4]) & rot[6]);
    temp1 = rot[7] + S1 + ch + TT.rconsttable64[i] + block[i];
    S0 = ror64(rot[0],28) ^ ror64(rot[0],34) ^ ror64(rot[0], 39);
    maj = (rot[0] & rot[1]) ^ (rot[0] & rot[2]) ^ (rot[1] & rot[2]);
    temp2 = S0 + maj;
    /*
    if (i < 3) {
      printf("  S1=%lu ch=%lu temp1=%lu S0=%lu maj=%lu temp2=%lu\n", S1,ch,temp1,S0,maj,temp2);
      printf("  rot[7]=%lu K[i]=%lu W[i]=%lu TT.buffer.i64[i]=%lu\n", rot[7],TT.rconsttable64[i],block[i],TT.buffer.i64[i]);
    }
    */
    rot[7] = rot[6];
    rot[6] = rot[5];
    rot[5] = rot[4];
    rot[4] = rot[3] + temp1;
    rot[3] = rot[2];
    rot[2] = rot[1];
    rot[1] = rot[0];
    rot[0] = temp1 + temp2;
    /*
    if ((i < 3) || (i > 77)) {
      //printf("after round %d: rot[0] = %lu  rot[1] = %lu  rot[2] = %lu\n", i, rot[0], rot[1], rot[2]);
      printf("t= %d: A=%08X%08X B=%08X%08X C= %08X%08X D=%08X%08X\n", i, \
        (uint32_t) (rot[0] >> 32), (uint32_t) rot[0], \
        (uint32_t) (rot[1] >> 32), (uint32_t) rot[1], \
        (uint32_t) (rot[2] >> 32), (uint32_t) rot[2], \
        (uint32_t) (rot[3] >> 32), (uint32_t) rot[3]  \
	);
      printf("t= %d: E=%08X%08X F=%08X%08X G= %08X%08X H=%08X%08X\n", i, \
        (uint32_t) (rot[4] >> 32), (uint32_t) rot[4], \
        (uint32_t) (rot[5] >> 32), (uint32_t) rot[5], \
        (uint32_t) (rot[6] >> 32), (uint32_t) rot[6], \
        (uint32_t) (rot[7] >> 32), (uint32_t) rot[7]  \
	);
    }
    */
  }
  //printf("%d rounds done: rot[0] = %lu  rot[1] = %lu  rot[2] = %lu\n", i, rot[0], rot[1], rot[2]);

  // Add the previous values of state.i64[]
  /*
      printf("t= %d: 0=%08X%08X 1=%08X%08X 2= %08X%08X 3=%08X%08X\n", -1, \
        (uint32_t) (TT.state.i64[0] >> 32), (uint32_t) TT.state.i64[0], \
        (uint32_t) (TT.state.i64[1] >> 32), (uint32_t) TT.state.i64[1], \
        (uint32_t) (TT.state.i64[2] >> 32), (uint32_t) TT.state.i64[2], \
        (uint32_t) (TT.state.i64[3] >> 32), (uint32_t) TT.state.i64[3]  \
	);
      printf("t= %d: 4=%08X%08X 5=%08X%08X 6= %08X%08X 7=%08X%08X\n", -1, \
        (uint32_t) (TT.state.i64[4] >> 32), (uint32_t) TT.state.i64[4], \
        (uint32_t) (TT.state.i64[5] >> 32), (uint32_t) TT.state.i64[5], \
        (uint32_t) (TT.state.i64[6] >> 32), (uint32_t) TT.state.i64[6], \
        (uint32_t) (TT.state.i64[7] >> 32), (uint32_t) TT.state.i64[7]  \
	);
  */
  for (i=0; i<8; i++) TT.state.i64[i] += rot[i];
  /*
      printf("t= %d: 0=%08X%08X 1=%08X%08X 2= %08X%08X 3=%08X%08X\n", -2, \
        (uint32_t) (TT.state.i64[0] >> 32), (uint32_t) TT.state.i64[0], \
        (uint32_t) (TT.state.i64[1] >> 32), (uint32_t) TT.state.i64[1], \
        (uint32_t) (TT.state.i64[2] >> 32), (uint32_t) TT.state.i64[2], \
        (uint32_t) (TT.state.i64[3] >> 32), (uint32_t) TT.state.i64[3]  \
	);
      printf("t= %d: 4=%08X%08X 5=%08X%08X 6= %08X%08X 7=%08X%08X\n", -2, \
        (uint32_t) (TT.state.i64[4] >> 32), (uint32_t) TT.state.i64[4], \
        (uint32_t) (TT.state.i64[5] >> 32), (uint32_t) TT.state.i64[5], \
        (uint32_t) (TT.state.i64[6] >> 32), (uint32_t) TT.state.i64[6], \
        (uint32_t) (TT.state.i64[7] >> 32), (uint32_t) TT.state.i64[7]  \
	);
  */
  //printf("state.i64[0] = %lu  state.i64[1] = %lu  state.i64[2] = %lu\n", TT.state.i64[0], TT.state.i64[1], TT.state.i64[2]);
}

static void sha384_transform(void)
{
  sha512_transform();
}

// Fill the 64/128-byte (512/1024-bit) working buffer and call transform() when full.

static void hash_update(char *data, unsigned int len, void (*transform)(void), int chunksize)
{
  unsigned int i, j;
  //printf("starting hash_update() TT.count = %llu len = %d chunksize = %d\n", TT.count,len,chunksize);

  j = TT.count & (chunksize - 1);
  TT.count += len;

  for (;;) {
    // Grab next chunk of data, return if it's not enough to process a frame
    i = chunksize - j;
    if (i>len) i = len;
    memcpy(TT.buffer.c+j, data, i);
    //printf("checking chunksize. j(%d) + i(%d) = chunksize(%d) ?\n", j,i,chunksize);
    if (j+i != chunksize) break;

    // Process a frame
    if (IS_BIG_ENDIAN) { // TODO: test on big endian architecture
      if ((TT.hashmethod == SHA512) || (TT.hashmethod == SHA384)) {
        for (j=0; j<16; j++) TT.buffer.i64[j] = SWAP_LE64(TT.buffer.i64[j]);
      } else { // MD5, SHA1, SHA224, SHA256
        for (j=0; j<16; j++) TT.buffer.i32[j] = SWAP_LE32(TT.buffer.i32[j]);
      }
    }
    //printf("calling transform. hashmethod = %d\n", TT.hashmethod);
    transform();
    j=0;
    data += i;
    len -= i;
  }
}

// Initialize array tersely
#define HASH_INIT(name, prefix) { name, (void *)prefix##_Init, \
  (void *)prefix##_Update, (void *)prefix##_Final, \
  prefix##_DIGEST_LENGTH, }
#define SHA1_DIGEST_LENGTH SHA_DIGEST_LENGTH

// Call the assembly optimized library code when CFG_TOYBOX_LIBCRYPTO
static void do_lib_hash(int fd, char *name)
{
  // Largest context
  SHA512_CTX ctx;
  struct hash {
    char *name;
    int (*init)(void *);
    int (*update)(void *, void *, size_t);
    int (*final)(void *, void *);
    int digest_length;
  } algorithms[] = {
    USE_TOYBOX_LIBCRYPTO(
      USE_MD5SUM(HASH_INIT("md5sum", MD5),)
      USE_SHA1SUM(HASH_INIT("sha1sum", SHA1),)
      USE_SHA224SUM(HASH_INIT("sha224sum", SHA224),)
      USE_SHA256SUM(HASH_INIT("sha256sum", SHA256),)
      USE_SHA384SUM(HASH_INIT("sha384sum", SHA384),)
      USE_SHA512SUM(HASH_INIT("sha512sum", SHA512),)
    )
  }, * hash;
  int i;

  // This should never NOT match, so no need to check
  for (i = 0; i<ARRAY_LEN(algorithms); i++)
    if (!strcmp(toys.which->name, algorithms[i].name)) break;
  hash = algorithms+i;

  hash->init(&ctx);
  for (;;) {
      i = read(fd, toybuf, sizeof(toybuf));
      if (i<1) break;
      hash->update(&ctx, toybuf, i);
  }
  hash->final(toybuf+128, &ctx);

  for (i = 0; i<hash->digest_length; i++)
    sprintf(toybuf+2*i, "%02x", toybuf[i+128]);
}

static void do_builtin_hash(int fd, char *name)
{
  uint64_t count;
  int i, chunksize, lengthsize, digestlen;
  char buf, *pp;
  void (*transform)(void);

  TT.count = 0;
  switch(TT.hashmethod) {
    case MD5:
    case SHA1:
      transform = (TT.hashmethod == MD5) ? md5_transform : sha1_transform;
      digestlen = (TT.hashmethod == MD5) ? 16 : 20; // bytes
      chunksize = 64; // bytes
      lengthsize = 8; // bytes
      TT.state.i32[0] = 0x67452301;
      TT.state.i32[1] = 0xEFCDAB89;
      TT.state.i32[2] = 0x98BADCFE;
      TT.state.i32[3] = 0x10325476;
      TT.state.i32[4] = 0xC3D2E1F0; // not used for MD5
      break;
    case SHA224:
      transform = sha224_transform;
      digestlen = 28;
      chunksize = 64;
      lengthsize = 8;
      TT.state.i32[0] = 0xc1059ed8;
      TT.state.i32[1] = 0x367cd507;
      TT.state.i32[2] = 0x3070dd17;
      TT.state.i32[3] = 0xf70e5939;
      TT.state.i32[4] = 0xffc00b31;
      TT.state.i32[5] = 0x68581511;
      TT.state.i32[6] = 0x64f98fa7;
      TT.state.i32[7] = 0xbefa4fa4;
      break;
    case SHA256:
      transform = sha256_transform;
      digestlen = 32;
      chunksize = 64;
      lengthsize = 8;
      TT.state.i32[0] = 0x6a09e667;
      TT.state.i32[1] = 0xbb67ae85;
      TT.state.i32[2] = 0x3c6ef372;
      TT.state.i32[3] = 0xa54ff53a;
      TT.state.i32[4] = 0x510e527f;
      TT.state.i32[5] = 0x9b05688c;
      TT.state.i32[6] = 0x1f83d9ab;
      TT.state.i32[7] = 0x5be0cd19;
      break;
    case SHA384:
      transform = sha384_transform;
      digestlen = 48;
      chunksize = 128;
      lengthsize = 8; // bytes. should be 16 according to spec
      TT.state.i64[0] = 0xcbbb9d5dc1059ed8;
      TT.state.i64[1] = 0x629a292a367cd507;
      TT.state.i64[2] = 0x9159015a3070dd17;
      TT.state.i64[3] = 0x152fecd8f70e5939;
      TT.state.i64[4] = 0x67332667ffc00b31;
      TT.state.i64[5] = 0x8eb44a8768581511;
      TT.state.i64[6] = 0xdb0c2e0d64f98fa7;
      TT.state.i64[7] = 0x47b5481dbefa4fa4;
      break;
    case SHA512:
      transform = sha512_transform;
      digestlen = 64;
      chunksize = 128;
      lengthsize = 8; // bytes. should be 16 according to spec
      TT.state.i64[0] = 0x6a09e667f3bcc908;
      TT.state.i64[1] = 0xbb67ae8584caa73b;
      TT.state.i64[2] = 0x3c6ef372fe94f82b;
      TT.state.i64[3] = 0xa54ff53a5f1d36f1;
      TT.state.i64[4] = 0x510e527fade682d1;
      TT.state.i64[5] = 0x9b05688c2b3e6c1f;
      TT.state.i64[6] = 0x1f83d9abfb41bd6b;
      TT.state.i64[7] = 0x5be0cd19137e2179;
      break;
    default: error_exit("unrecognized hash method name"); break;
    }

  for (;;) {
    i = read(fd, toybuf, sizeof(toybuf));
    if (i<1) break;
    hash_update(toybuf, i, transform, chunksize);
  }

  count = TT.count << 3; // convert to bytes

  // End the message by appending a "1" bit to the data, ending with the
  // message size (in bits, big endian), and adding enough zero bits in
  // between to pad to the end of the next 64-byte frame.
  //
  // Since our input up to now has been in whole bytes, we can deal with
  // bytes here too.
  buf = 0x80;
  do {
    hash_update(&buf, 1, transform, chunksize);
    buf = 0;
  } while ((TT.count & (chunksize - 1)) != (chunksize - lengthsize));
  count = (TT.hashmethod == MD5) ? SWAP_LE64(count) : SWAP_BE64(count);
  //printf("count=%ld count=%08X %08X\n", count, (uint32_t) (count >> 32), (uint32_t) count);
  hash_update((void *)&count, 8, transform, chunksize);

  // write digest to toybuf
  if ((TT.hashmethod == SHA384) || (TT.hashmethod == SHA512)) {
    for (i=0; i<digestlen/8; i++) {
      sprintf(toybuf+16*i, "%016lx", TT.state.i64[i]);
    }
  } else { // MD5, SHA1, SHA224, SHA256
    for (i=0; i<digestlen/4; i++) {
      sprintf(toybuf+8*i, "%08x",
              (TT.hashmethod == MD5) ? bswap_32(TT.state.i32[i]) : TT.state.i32[i]
       );
    }
  }
  // Wipe variables. Cryptographer paranoia.
  // if we do this with memset(), gcc throws a broken warning, and the (uint32_t)
  // typecasts stop gcc from breaking "undefined behavior" that isn't.
  for (pp = (void *)TT.state.i64; (uint64_t)pp-(uint64_t)TT.state.i64<sizeof(TT)-((uint64_t)TT.state.i64-(uint64_t)&TT); pp++)
    *pp = 0;
  i = strlen(toybuf)+1;
  memset(toybuf+i, 0, sizeof(toybuf)-i);
}

// Callback for loopfiles()
// Call builtin or lib hash function, then display output if necessary
static void do_hash(int fd, char *name)
{
  if (CFG_TOYBOX_LIBCRYPTO) do_lib_hash(fd, name);
  else do_builtin_hash(fd, name);

  if (name)
    printf(FLAG(b) ? "%s\n" : "%s  %s\n", toybuf, name);
}

static int do_c_line(char *line)
{
  int space = 0, fail = 0;
  char *name;

  for (name = line; *name; name++) {
    if (isspace(*name)) {
      space++;
      *name = 0;
    } else if (space) break;
  }

  if (!space || !*line || !*name) error_msg("bad line %s", line);
  else {
    int fd = !strcmp(name, "-") ? 0 : open(name, O_RDONLY);

    TT.sawline = 1;
    if (fd==-1) {
      perror_msg_raw(name);
      *toybuf = 0;
    } else do_hash(fd, 0);
    if (strcasecmp(line, toybuf)) toys.exitval = fail = 1;
    if (!FLAG(s)) printf("%s: %s\n", name, fail ? "FAILED" : "OK");
    if (fd>0) close(fd);
  }

  return 0;
}

// Used instead of loopfiles_line to report error on files containing no hashes.
static void do_c_file(char *name)
{
  FILE *fp = !strcmp(name, "-") ? stdin : fopen(name, "r");

  if (!fp) {
    perror_msg_raw(name);
    return;
  }

  TT.sawline = 0;

  for (;;) {
    char *line = 0;
    ssize_t len;

    if ((len = getline(&line, (void *)&len, fp))<1) break;
    if (line[len-1]=='\n') line[len-1] = 0;
    do_c_line(line);
    free(line);
  }
  if (fp!=stdin) fclose(fp);

  if (!TT.sawline) error_msg("%s: no lines", name);
}

void md5sum_main(void)
{
  char **arg;
  int i;

  if (toys.which->name[0]=='m') {
    TT.hashmethod = MD5;
  }

  // Calculate table if we have floating point. Static version should drop
  // out at compile time when we don't need it.
  if (!CFG_TOYBOX_LIBCRYPTO) {
    switch(TT.hashmethod) {
      case MD5:
        if (CFG_TOYBOX_FLOAT) {
          TT.rconsttable32 = xmalloc(64*4);
          for (i = 0; i<64; i++) TT.rconsttable32[i] = fabs(sin(i+1))*(1LL<<32);
        } else TT.rconsttable32 = md5nofloat;
	break;
      case SHA1: // no table needed for SHA1
	break;
      case SHA224:
      case SHA256:
        TT.rconsttable32 = xmalloc(64*4);
        if (CFG_TOYBOX_FLOAT) {
          // first 32 bits of the fractional parts of the cube roots of the first 64 primes 2..311
	  uint16_t prime = 2;
	  i = 0;
          for (i=0; i<64; i++) {
            TT.rconsttable32[i] = (uint32_t) (fmod(cbrt(prime), 1.0) * pow(2,32));
	    //printf("i=%d\tanswer=%08x\trconst=%08x\tprime=%d\tcbrt=%.8f\n",i,(uint32_t) (sha512nofloat[i] >> 32),TT.rconsttable32[i],prime,cbrt( (double) prime ));
	    prime += primegaps[i];
          }
        } else {
          for (i=0; i<64; i++) {
            TT.rconsttable32[i] = (uint32_t) (sha512nofloat[i] >> 32);
          }
	}
	break;
      case SHA384:
      case SHA512:
        if (CFG_TOYBOX_FLOAT) {
          TT.rconsttable64 = xmalloc(80*8);
          // first 64 bits of the fractional parts of the cube roots of the first 80 primes 2..409
	  uint16_t prime = 2;
	  long double primecbrt;
	  i = 0;
          for (i=0; i<80; i++) {
            primecbrt = cbrt(prime);
            TT.rconsttable64[i] = (uint64_t) ((primecbrt - (long double) floor(primecbrt)) * pow(2,64));
	    //printf("i=%d\tanswer=%016lx\trconst=%016lx\tprime=%d\tcbrt=%.40Lf\n",i,sha512nofloat[i],TT.rconsttable64[i],prime,primecbrt);
	    prime += primegaps[i];
          }
        } else {
          TT.rconsttable64 = sha512nofloat;
	}
	break;
      default: error_exit("unrecognized hash method name"); break;
    }
  }

  if (FLAG(c)) for (arg = toys.optargs; *arg; arg++) do_c_file(*arg);
  else {
    if (FLAG(s)) error_exit("-s only with -c");
    loopfiles(toys.optargs, do_hash);
  }
}

void sha1sum_main(void)
{
  TT.hashmethod = SHA1;
  md5sum_main();
}

void sha224sum_main(void)
{
  TT.hashmethod = SHA224;
  md5sum_main();
}

void sha256sum_main(void)
{
  TT.hashmethod = SHA256;
  md5sum_main();
}

void sha384sum_main(void)
{
  TT.hashmethod = SHA384;
  md5sum_main();
}

void sha512sum_main(void)
{
  TT.hashmethod = SHA512;
  md5sum_main();
}
