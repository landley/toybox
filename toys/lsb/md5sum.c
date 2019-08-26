/* md5sum.c - Calculate RFC 1321 md5 hash and sha1 hash.
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/md5sum.html
 * and http://www.ietf.org/rfc/rfc1321.txt
 *
 * They're combined this way to share infrastructure, and because md5sum is
 * and LSB standard command (but sha1sum and newer hashes are a good idea,
 * see http://valerieaurora.org/hash.html).
 *
 * We optionally use openssl (or equivalent) to access assembly optimized
 * versions of these functions, but provide a built-in version to reduce
 * required dependencies.
 *
 * coreutils supports --status but not -s, busybox supports -s but not --status

USE_MD5SUM(NEWTOY(md5sum, "bc(check)s(status)[!bc]", TOYFLAG_USR|TOYFLAG_BIN))
USE_SHA1SUM(NEWTOY(sha1sum, "bc(check)s(status)[!bc]", TOYFLAG_USR|TOYFLAG_BIN))
USE_TOYBOX_LIBCRYPTO(USE_SHA224SUM(OLDTOY(sha224sum, sha1sum, TOYFLAG_USR|TOYFLAG_BIN)))
USE_TOYBOX_LIBCRYPTO(USE_SHA256SUM(OLDTOY(sha256sum, sha1sum, TOYFLAG_USR|TOYFLAG_BIN)))
USE_TOYBOX_LIBCRYPTO(USE_SHA384SUM(OLDTOY(sha384sum, sha1sum, TOYFLAG_USR|TOYFLAG_BIN)))
USE_TOYBOX_LIBCRYPTO(USE_SHA512SUM(OLDTOY(sha512sum, sha1sum, TOYFLAG_USR|TOYFLAG_BIN)))

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
  depends on TOYBOX_LIBCRYPTO
  help
    See sha1sum

config SHA256SUM
  bool "sha256sum"
  default y
  depends on TOYBOX_LIBCRYPTO
  help
    See sha1sum

config SHA384SUM
  bool "sha384sum"
  default y
  depends on TOYBOX_LIBCRYPTO
  help
    See sha1sum

config SHA512SUM
  bool "sha512sum"
  default y
  depends on TOYBOX_LIBCRYPTO
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

  // Crypto variables blanked after summing
  unsigned state[5];
  unsigned oldstate[5];
  uint64_t count;
  union {
    char c[64];
    unsigned i[16];
  } buffer;
)

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

// Mix next 64 bytes of data into md5 hash

static void md5_transform(void)
{
  unsigned x[4], *b = (unsigned *)TT.buffer.c;
  int i;

  memcpy(x, TT.state, sizeof(x));

  for (i=0; i<64; i++) {
    unsigned int in, a, rot, temp;

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
    temp += x[a] + b[in] + md5table[i];
    x[a] = x[(a+1)&3] + ((temp<<rot) | (temp>>(32-rot)));
  }
  for (i=0; i<4; i++) TT.state[i] += x[i];
}

// Mix next 64 bytes of data into sha1 hash.

static const unsigned rconsts[]={0x5A827999,0x6ED9EBA1,0x8F1BBCDC,0xCA62C1D6};
#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

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

// Callback for loopfiles()

static void do_builtin_hash(int fd, char *name)
{
  uint64_t count;
  int i, sha1=toys.which->name[0]=='s';
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
      sprintf(toybuf+2*i, "%02x", 255&(TT.state[i>>2] >> ((3-(i & 3)) * 8)));
  else for (i=0; i<4; i++) sprintf(toybuf+8*i, "%08x", bswap_32(TT.state[i]));

  // Wipe variables. Cryptographer paranoia.
  memset(TT.state, 0, sizeof(TT)-((long)TT.state-(long)&TT));
  i = strlen(toybuf)+1;
  memset(toybuf+i, 0, sizeof(toybuf)-i);
}

// Call builtin or lib hash function, then display output if necessary
static void do_hash(int fd, char *name)
{
  if (CFG_TOYBOX_LIBCRYPTO) do_lib_hash(fd, name);
  else do_builtin_hash(fd, name);

  if (name)
    printf((toys.optflags & FLAG_b) ? "%s\n" : "%s  %s\n", toybuf, name);
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

  if (FLAG(c)) for (arg = toys.optargs; *arg; arg++) do_c_file(*arg);
  else {
    if (FLAG(s)) error_exit("-s only with -c");
    loopfiles(toys.optargs, do_hash);
  }
}

void sha1sum_main(void)
{
  md5sum_main();
}
