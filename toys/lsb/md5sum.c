/* md5sum.c - Calculate RFC 1321 md5 hash and sha1 hash.
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/md5sum.html
 * and http://www.ietf.org/rfc/rfc1321.txt
 *
 * They're combined this way to share infrastructure, and because md5sum is
 * and LSB standard command, sha1sum is just a good idea.

USE_MD5SUM(NEWTOY(md5sum, "b", TOYFLAG_USR|TOYFLAG_BIN))
USE_SHA1SUM(NEWTOY(sha1sum, "b", TOYFLAG_USR|TOYFLAG_BIN))

config MD5SUM
  bool "md5sum"
  default y
  help
    usage: md5sum [FILE]...

    Calculate md5 hash for each input file, reading from stdin if none.
    Output one hash (16 hex digits) for each input file, followed by
    filename.

    -b	brief (hash only, no filename)

config SHA1SUM
  bool "sha1sum"
  default y
  help
    usage: sha1sum [FILE]...

    calculate sha1 hash for each input file, reading from stdin if none.
    Output one hash (20 hex digits) for each input file, followed by
    filename.

    -b	brief (hash only, no filename)
*/

#define FOR_md5sum
#include "toys.h"

GLOBALS(
  unsigned state[5];
  unsigned oldstate[5];
  uint64_t count;
  union {
    char c[64];
    unsigned i[16];
  } buffer;
)

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

// for(i=0; i<64; i++) md5table[i] = abs(sin(i+1))*(1<<32);  But calculating
// that involves not just floating point but pulling in -lm (and arguing with
// C about whether 1<<32 is a valid thing to do on 32 bit platforms) so:

static uint32_t md5table[64] = {
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

static const uint8_t md5rot[64] = {
  7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
  5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

// Mix next 64 bytes of data into md5 hash

static void md5_transform(void)
{
  unsigned x[4], *b = TT.buffer.i;
  int i;

  memcpy(x, TT.state, sizeof(x));

  for (i=0; i<64; i++) {
    unsigned int in, temp, swap;
    if (i<16) {
      in = i;
      temp = x[1];
      temp = (temp & x[2]) | ((~temp) & x[3]);
    } else if (i<32) {
      in = (1+(5*i))&15;
      temp = x[3];
      temp = (x[1] & temp) | (x[2] & ~temp);
    } else if (i<48) {
      in = (3*i+5)&15;
      temp = x[1] ^ x[2] ^ x[3];
    } else {
      in = (7*i)&15;
      temp = x[2] ^ (x[1] | ~x[3]);
    }
    temp += x[0] + b[in] + md5table[i];
    swap = x[3];
    x[3] = x[2];
    x[2] = x[1];
    x[1] += rol(temp, md5rot[i]);
    x[0] = swap;
  }
  for (i=0; i<4; i++) TT.state[i] += x[i];
}

// Mix next 64 bytes of data into sha1 hash.

static const unsigned rconsts[]={0x5A827999,0x6ED9EBA1,0x8F1BBCDC,0xCA62C1D6};

static void sha1_transform(void)
{
  int i, j, k, count;
  unsigned *block = TT.buffer.i;
  unsigned *rot[5], *temp;

  // Copy context->state[] to working vars
  for (i=0; i<5; i++) {
    TT.oldstate[i] = TT.state[i];
    rot[i] = TT.state + i;
  }
  // 4 rounds of 20 operations each.
  for (i=count=0; i<4; i++) {
    for (j=0; j<20; j++) {
      unsigned work;

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
      *rot[4] += work + rol(*rot[0],5) + rconsts[i];
      *rot[1] = rol(*rot[1],30);

      // Rotate by one for next time.
      temp = rot[4];
      for (k=4; k; k--) rot[k] = rot[k-1];
      *rot = temp;
      count++;
    }
  }
  // Add the previous values of state[]
  for (i=0; i<5; i++) TT.state[i] += TT.oldstate[i];
}

// Fill the 64-byte working buffer and call transform() when full.

static void hash_update(char *data, unsigned int len, void (*transform)(void))
{
  unsigned int i, j;

  j = TT.count & 63;
  TT.count += len;

  for (;;) {
    // Grab next chunk of data, return if it's not enough to process a frame
    i = 64 - j;
    if (i>len) i = len;
    memcpy(TT.buffer.c+j, data, i);
    if (j+i != 64) break;

    // Process a frame
    if (IS_BIG_ENDIAN)
      for (j=0; j<16; j++) TT.buffer.i[j] = SWAP_LE32(TT.buffer.i[j]);
    transform();
    j=0;
    data += i;
    len -= i;
  }
}

// Callback for loopfiles()

static void do_hash(int fd, char *name)
{
  uint64_t count;
  int i, sha1=toys.which->name[0]=='s';;
  char buf;
  void (*transform)(void);

  /* SHA1 initialization constants  (md5sum uses first 4) */
  TT.state[0] = 0x67452301;
  TT.state[1] = 0xEFCDAB89;
  TT.state[2] = 0x98BADCFE;
  TT.state[3] = 0x10325476;
  TT.state[4] = 0xC3D2E1F0;
  TT.count = 0;

  transform = sha1 ? sha1_transform : md5_transform;
  for (;;) {
    i = read(fd, toybuf, sizeof(toybuf));
    if (i<1) break;
    hash_update(toybuf, i, transform);
  }

  count = TT.count << 3;

  // End the message by appending a "1" bit to the data, ending with the
  // message size (in bits, big endian), and adding enough zero bits in
  // between to pad to the end of the next 64-byte frame.
  //
  // Since our input up to now has been in whole bytes, we can deal with
  // bytes here too.

  buf = 0x80;
  do {
    hash_update(&buf, 1, transform);
    buf = 0;
  } while ((TT.count & 63) != 56);
  count = sha1 ? SWAP_BE64(count) : SWAP_LE64(count);
  hash_update((void *)&count, 8, transform);

  if (sha1)
    for (i = 0; i < 20; i++)
      printf("%02x", 255&(TT.state[i>>2] >> ((3-(i & 3)) * 8)));
  else for (i=0; i<4; i++) printf("%08x", bswap_32(TT.state[i]));

  // Wipe variables. Cryptographer paranoia.
  memset(&TT, 0, sizeof(TT));

  printf((toys.optflags & FLAG_b) ? "\n" : "  %s\n", name);
}

void md5sum_main(void)
{
  loopfiles(toys.optargs, do_hash);
}

void sha1sum_main(void)
{
  md5sum_main();
}
