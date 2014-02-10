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

  unsigned (*crc)(char *data, int len);

  char *outbuf;
  unsigned outlen;
  int outfd;
)

// little endian bit buffer
struct bitbuf {
  int fd, bitpos, len, max;
  char buf[];
};

// malloc a struct bitbuf
struct bitbuf *bitbuf_init(int fd, int size)
{
  struct bitbuf *bb = xmalloc(sizeof(struct bitbuf)+size);

  memset(bb, 0, sizeof(struct bitbuf));
  bb->max = size;
  bb->fd = fd;

  return bb;
}

// Advance bitpos without the overhead of recording bits
void bitbuf_skip(struct bitbuf *bb, int bits)
{
  int pos = bb->bitpos + bits, len = bb->len << 3;

  while (pos >= len) {
    pos -= len;
    len = (bb->len = read(bb->fd, bb->buf, bb->max)) << 3;
    if (bb->len < 1) perror_exit("inflate EOF");
  }
  bb->bitpos = pos;
}

// Optimized single bit inlined version
static inline int bitbuf_bit(struct bitbuf *bb)
{
  int bufpos = bb->bitpos>>3;

  if (bufpos == bb->len) {
    bitbuf_skip(bb, 0);
    bufpos = 0;
  }

  return (bb->buf[bufpos]>>(bb->bitpos++&7))&1;
}

// Fetch the next X bits from the bitbuf, little endian
int bitbuf_get(struct bitbuf *bb, int bits)
{
  int result = 0, offset = 0;

  while (bits) {
    int click = bb->bitpos >> 3, blow, blen;

    // Load more data if buffer empty
    if (click == bb->len) bitbuf_skip(bb, 0);

    // grab bits from next byte
    blow = bb->bitpos & 7;
    blen = 8-blow;
    if (blen > bits) blen = bits;
    result |= ((bb->buf[click] >> blow) & ((1<<blen)-1)) << offset;
    offset += blen;
    bits -= blen;
    bb->bitpos += blen;
  }

  return result;
}

static void outbuf_crc(char *buf, int len)
{
  if (TT.crc) TT.crc(buf, len);
  xwrite(TT.outfd, buf, len);
}

// Huffman coding uses bits to traverse a binary tree to a leaf node,
// By placing frequently occurring symbols at shorter paths, frequently
// used symbols may be represented in fewer bits than uncommon symbols.

struct huff {
  unsigned short length[16];
  unsigned short symbol[288];
};

// Create simple huffman tree from array of bit lengths.

// The symbols in deflate's huffman trees are sorted (first by bit length
// of the code to reach them, then by symbol number). This means that given
// the bit length of each symbol, we can construct a unique tree.
static void len2huff(struct huff *huff, char bitlen[], int len)
{
  int offset[16];
  int i, sum;

  // Count number of codes at each bit length
  memset(huff, 0, sizeof(struct huff));
  for (i = 0; i<len; i++) huff->length[bitlen[i]]++;

  // Sort symbols by bit length. (They'll remain sorted by symbol within that.)
  *huff->length = *offset = 0;
  for (i = sum = 0; i<16; i++) offset[i] = offset[i-1] + huff->length[i];
  for (i = 0; i<len; i++) if (bitlen[i]) huff->symbol[offset[bitlen[i]]++] = i;
}

// Fetch and decode next huffman coded symbol from bitbuf.
// This takes advantage of the the sorting to navigate the tree as an array:
// each time we fetch a bit we have all the codes at that bit level in
// order with no gaps..
static unsigned huff_and_puff(struct bitbuf *bb, struct huff *huff)
{
  unsigned short *length = huff->length;
  int start = 0, offset = 0;

  // Traverse through the bit lengths until our code is in this range
  for (;;) {
    offset = (offset << 1) | bitbuf_bit(bb);
    start += *++length;
    if ((offset -= *length) < 0) break;
// Note to self: what prevents overflow?
// If we ensure ranges add up to sizeof(symbol), does that ensure all codes
// terminate within table? Patholotical is 11111111...
//    if (length - huff_length > 16) error_exit("bad");
  }

  return huff->symbol[start + offset];
}

// Decompress deflated data from bitbuf to filehandle.
static void inflate(struct bitbuf *bb)
{
  // repeat until spanked
  for (;;) {
    int final, type;

    final = bitbuf_get(bb, 1);
    type = bitbuf_get(bb, 2);
fprintf(stderr, "final=%d type=%d\n", final, type);

    if (type == 3) error_exit("bad type");

    // no compression?
    if (!type) {
      int len, nlen;

      // Align to byte, read length
      bitbuf_skip(bb, bb->bitpos & 7);
      len = bitbuf_get(bb, 16);
      nlen = bitbuf_get(bb, 16);
      if (len != (0xffff & ~nlen)) error_exit("bad len");

      // Dump output data
      while (len) {
        int pos = bb->bitpos >> 3, bblen = bb->len - pos;

        if (bblen > len) bblen = len;
        if (bblen) outbuf_crc(bb->buf+pos, bblen);
        len -= bblen;
        bitbuf_skip(bb, bblen << 3);
      }

    // Compressed block
    } else {
//      struct huff huff;

      // Dynamic huffman codes?
      if (type == 2) {
        struct huff hlo;
        int i, literal, distance, hufflen;
        char *hufflen_order = "\x10\x11\x12\0\x08\x07\x09\x06\x0a\x05\x0b"
                              "\x04\x0c\x03\x0d\x02\x0e\x01\x0f", *bits;

        // The huffman trees are stored as a series of bit lengths
        literal = bitbuf_get(bb, 5)+257; // max 288
        distance = bitbuf_get(bb, 5)+1;  // max 32
        hufflen = bitbuf_get(bb, 4)+4;   // max 19

        // The literal and distance codes are themselves compressed, in
        // a complicated way: an array of bit lengths (hufflen many
        // entries, each 3 bits) is used to fill out an array of 19 entries
        // in a magic order, leaving the rest 0. Then make a tree out of it:
        memset(bits = toybuf+1, 0, 19);
        for (i=0; i<hufflen; i++) bits[hufflen_order[i]] = bitbuf_get(bb, 3);
        len2huff(&hlo, bits, 19);

        // Use that tree to read in the literal and distance bit lengths
        for (i = 0; i < literal + distance; i++) {
          int sym = huff_and_puff(bb, &hlo);

          // 0-15 are literals, 16 = repeat previous code 3-6 times,
          // 17 = 3-10 zeroes, 18 = 11-138 zeroes
          if (sym < 16) bits[i] = sym;
          else memset(bits+i, bits[i-1] * !(sym&3),
                      bitbuf_get(bb, (sym-14)+((sym&2)<<2)));
        }
      // Static huffman codes
      } else {
      }
    }

    if (final) return;
  }
}

static void init_deflate(void)
{
  int i, n = 1;

  // Ye olde deflate window
  TT.outbuf = xmalloc(32768);

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
  if (bitbuf_get(bb, 24) != 0x088b1f || (flags = bitbuf_get(bb, 8)) > 31)
    return 0;
  bitbuf_skip(bb, 6*8);

  // Skip extra, name, comment, header CRC fields
  if (flags & 4) bitbuf_skip(bb, 16);
  if (flags & 8) while (bitbuf_get(bb, 8));
  if (flags & 16) while (bitbuf_get(bb, 8));
  if (flags & 2) bitbuf_skip(bb, 16);

  return 1;
}

static void do_gzip(int fd, char *name)
{
  struct bitbuf *bb = bitbuf_init(fd, sizeof(toybuf));

  if (!is_gzip(bb)) error_exit("not gzip");
  TT.outfd = 1;
  inflate(bb);

  // tail: crc32, len32

  free(bb);
}

// Parse many different kinds of command line argument:

void compress_main(void)
{
  init_deflate();

  loopfiles(toys.optargs, do_gzip);
}
