/* compress.c - deflate/inflate code for zip, gzip, zlib, and raw
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * The inflate/deflate code lives here, so the various things that use it
 * either live here or call these commands to pipe data through them.
 *
 * Divergence from posix: replace obsolete "compress" with mutiplexer.
 *
 * See RFCs 1950 (zlib), 1951 (deflate), and 1952 (gzip)
 * LSB 4.1 has gzip, gunzip, and zcat
 * TODO: zip -d DIR -x LIST -list -quiet -no overwrite -overwrite -p to stdout

// Accept many different kinds of command line argument:

USE_COMPRESS(NEWTOY(compress, "zglrcd9[-cd][!zglr]", TOYFLAG_USR|TOYFLAG_BIN))

//zip unzip gzip gunzip zcat

config COMPRESS
  bool "compress"
  default n
  help
    usage: compress [-zglrcd9] [FILE]

    Compress or decompress file (or stdin) using "deflate" algorithm.

    -c	compress
    -d	decompress
    -g	gzip
    -l	zlib
    -r	raw (default)
    -z	zip
*/

#define FOR_compress
#include "toys.h"

GLOBALS(
  // base offset and extra bits tables (length and distance)
  char lenbits[29], distbits[30];
  unsigned short lenbase[29], distbase[30];
)

// little endian bit buffer
struct bitbuf {
  int fd, pos, len, max;
  char buf[];
};

struct bitbuf *get_bitbuf(int fd, int size)
{
  struct bitbuf *bb = xmalloc(sizeof(struct bitbuf)+size);

  memset(bb, 0, sizeof(struct bitbuf));
  bb->max = size;
  bb->fd = fd;

  return bb;
}

int get_bits(struct bitbuf *bb, int bits)
{
  int result = 0, offset = 0;

  while (bits) {
    int click = bb->pos >> 3, blow, blen;

    // Load more data if buffer empty
    if (click == bb->len) {
      bb->len = read(bb->fd, bb->buf, bb->max);
      if (bb->len < 1) perror_exit("inflate EOF");
      bb->pos = click = 0;
    }

    // grab bits from next byte
    blow = bb->pos & 7;
    blen = 8-blow;
    if (blen > bits) blen = bits;
    result |= ((bb->buf[click] >> blow) & ((1<<blen)-1)) << offset;
    offset += blen;
    bits -= blen;
    bb->pos += blen;
  }

  return result;
}

static int deflate(struct bitbuf *buf)
{
  return 0;
}

static void init_deflate(void)
{
  int i, n = 1;

  // Calculate lenbits, lenbase, distbits, distbase
  *TT.lenbase = 3;
  for (i = 0; i<sizeof(TT.lenbits)-1; i++) {
    if (i>4) {
      if (!(i&3)) {
        TT.lenbits[i]++;
        n <<= 1;
      }
      if (i == 27) n--;
      else TT.lenbits[i+1] = TT.lenbits[i];
    }
    TT.lenbase[i+1] = n + TT.lenbase[i];
  }
  n = 0;
  for (i = 0; i<sizeof(TT.distbits); i++) {
    TT.distbase[i] = 1<<n;
    if (i) TT.distbase[i] += TT.distbase[i-1];
    if (i>3 && !(i&1)) n++;
    TT.distbits[i] = n;
  }
}

// Return true/false whether we consumed a gzip header.
static int is_gzip(struct bitbuf *bb)
{
  int flags;

  // Confirm signature
  if (get_bits(bb, 24) != 0x088b1f || (flags = get_bits(bb, 8)) > 31) return 0;
  get_bits(bb, 6*8);

  // Skip extra, name, comment, header CRC fields
  if (flags & 4) get_bits(bb, 16);
  if (flags & 8) while (get_bits(bb, 8));
  if (flags & 16) while (get_bits(bb, 8));
  if (flags & 2) get_bits(bb, 16);

  return 1;
}

static void do_gzip(int fd, char *name)
{
  struct bitbuf *bb = get_bitbuf(fd, sizeof(toybuf));

  printf("is_gzip=%d\n", is_gzip(bb));

  // tail: crc32, len32

  free(bb);
}

// Parse many different kinds of command line argument:

void compress_main(void)
{
  // 31, 139, 8
  // &2 = skip 2 bytes
  init_deflate();

  loopfiles(toys.optargs, do_gzip);
}
