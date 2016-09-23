/* compress.c - deflate/inflate code for zip, gzip, zlib, and raw
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * The inflate/deflate code lives here, so the various things that use it
 * either live here or call these commands to pipe data through them.
 *
 * Divergence from posix: replace obsolete/patented "compress" with mutiplexer.
 * (gzip already replaces "uncompress".)
 *
 * See RFCs 1950 (zlib), 1951 (deflate), and 1952 (gzip)
 * LSB 4.1 has gzip, gunzip, and zcat
 * TODO: zip -d DIR -x LIST -list -quiet -no overwrite -overwrite -p to stdout

// Accept many different kinds of command line argument.
// Leave Lrg at end so flag values line up.

USE_COMPRESS(NEWTOY(compress, "zcd9lrg[-cd][!zgLr]", TOYFLAG_USR|TOYFLAG_BIN))

//zip unzip gzip gunzip zcat

config COMPRESS
  bool "compress"
  default n
  help
    usage: compress [-zgLR19] [FILE]

    Compress or decompress file (or stdin) using "deflate" algorithm.

    -1	min compression
    -9	max compression (default)
    -g	gzip (default)
    -L	zlib
    -R	raw
    -z	zip

config DECOMPRESS
  bool "decompress"
  default n
  help
    usage: compress [-zglrcd9] [FILE]

    Compress or decompress file (or stdin) using "deflate" algorithm.

    -c	compress with -g gzip (default)  -l zlib  -r raw  -z zip
    -d	decompress (autodetects type)
*/

#define FOR_compress
#include "toys.h"

GLOBALS(
  // Huffman codes: base offset and extra bits tables (length and distance)
  char lenbits[29], distbits[30];
  unsigned short lenbase[29], distbase[30];
  void *fixdisthuff, *fixlithuff;

  // CRC
  void (*crcfunc)(char *data, int len);
  unsigned crc;

  // Compressed data buffer
  char *data;
  unsigned pos, len;
  int infd, outfd;

  // Tables only used for deflation
  unsigned short *hashhead, *hashchain;
)

// little endian bit buffer
struct bitbuf {
  int fd, bitpos, len, max;
  char buf[];
};

// malloc a struct bitbuf
struct bitbuf *bitbuf_init(int fd, int size)
{
  struct bitbuf *bb = xzalloc(sizeof(struct bitbuf)+size);

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
unsigned bitbuf_get(struct bitbuf *bb, int bits)
{
  int result = 0, offset = 0;

  while (bits) {
    int click = bb->bitpos >> 3, blow, blen;

    // Load more data if buffer empty
    if (click == bb->len) bitbuf_skip(bb, click = 0);

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

void bitbuf_flush(struct bitbuf *bb)
{
  if (!bb->bitpos) return;

  xwrite(bb->fd, bb->buf, (bb->bitpos+7)/8);
  memset(bb->buf, 0, bb->max);
  bb->bitpos = 0;
}

void bitbuf_put(struct bitbuf *bb, int data, int len)
{
  while (len) {
    int click = bb->bitpos >> 3, blow, blen;

    // Flush buffer if necessary
    if (click == bb->max) {
      bitbuf_flush(bb);
      click = 0;
    }
    blow = bb->bitpos & 7;
    blen = 8-blow;
    if (blen > len) blen = len;
    bb->buf[click] |= data << blow;
    bb->bitpos += blen;
    data >>= blen;
    len -= blen;
  }
}

static void output_byte(char sym)
{
  int pos = TT.pos++ & 32767;

  TT.data[pos] = sym;

  if (pos == 32767) {
    xwrite(TT.outfd, TT.data, 32768);
    if (TT.crcfunc) TT.crcfunc(TT.data, 32768);
  }
}

// Huffman coding uses bits to traverse a binary tree to a leaf node,
// By placing frequently occurring symbols at shorter paths, frequently
// used symbols may be represented in fewer bits than uncommon symbols.

struct huff {
  unsigned short length[16];
  unsigned short symbol[288];
};

// Create simple huffman tree from array of bit lengths.

// The symbols in the huffman trees are sorted (first by bit length
// of the code to reach them, then by symbol number). This means that given
// the bit length of each symbol, we can construct a unique tree.
static void len2huff(struct huff *huff, char bitlen[], int len)
{
  int offset[16];
  int i;

  // Count number of codes at each bit length
  memset(huff, 0, sizeof(struct huff));
  for (i = 0; i<len; i++) huff->length[bitlen[i]]++;

  // Sort symbols by bit length. (They'll remain sorted by symbol within that.)
  *huff->length = *offset = 0;
  for (i = 1; i<16; i++) offset[i] = offset[i-1] + huff->length[i-1];

  for (i = 0; i<len; i++) if (bitlen[i]) huff->symbol[offset[bitlen[i]]++] = i;
}

// Fetch and decode next huffman coded symbol from bitbuf.
// This takes advantage of the sorting to navigate the tree as an array:
// each time we fetch a bit we have all the codes at that bit level in
// order with no gaps.
static unsigned huff_and_puff(struct bitbuf *bb, struct huff *huff)
{
  unsigned short *length = huff->length;
  int start = 0, offset = 0;

  // Traverse through the bit lengths until our code is in this range
  for (;;) {
    offset = (offset << 1) | bitbuf_bit(bb);
    start += *++length;
    if ((offset -= *length) < 0) break;
    if ((length - huff->length) & 16) error_exit("bad symbol");
  }

  return huff->symbol[start + offset];
}

// Decompress deflated data from bitbuf to TT.outfd.
static void inflate(struct bitbuf *bb)
{
  TT.crc = ~0;
  // repeat until spanked
  for (;;) {
    int final, type;

    final = bitbuf_get(bb, 1);
    type = bitbuf_get(bb, 2);

    if (type == 3) error_exit("bad type");

    // Uncompressed block?
    if (!type) {
      int len, nlen;

      // Align to byte, read length
      bitbuf_skip(bb, (8-bb->bitpos)&7);
      len = bitbuf_get(bb, 16);
      nlen = bitbuf_get(bb, 16);
      if (len != (0xffff & ~nlen)) error_exit("bad len");

      // Dump literal output data
      while (len) {
        int pos = bb->bitpos >> 3, bblen = bb->len - pos;
        char *p = bb->buf+pos;

        // dump bytes until done or end of current bitbuf contents
        if (bblen > len) bblen = len;
        pos = bblen;
        while (pos--) output_byte(*(p++));
        bitbuf_skip(bb, bblen << 3);
        len -= bblen;
      }

    // Compressed block
    } else {
      struct huff *disthuff, *lithuff;

      // Dynamic huffman codes?
      if (type == 2) {
        struct huff *h2 = ((struct huff *)toybuf)+1;
        int i, litlen, distlen, hufflen;
        char *hufflen_order = "\x10\x11\x12\0\x08\x07\x09\x06\x0a\x05\x0b"
                              "\x04\x0c\x03\x0d\x02\x0e\x01\x0f", *bits;

        // The huffman trees are stored as a series of bit lengths
        litlen = bitbuf_get(bb, 5)+257;  // max 288
        distlen = bitbuf_get(bb, 5)+1;   // max 32
        hufflen = bitbuf_get(bb, 4)+4;   // max 19

        // The literal and distance codes are themselves compressed, in
        // a complicated way: an array of bit lengths (hufflen many
        // entries, each 3 bits) is used to fill out an array of 19 entries
        // in a magic order, leaving the rest 0. Then make a tree out of it:
        memset(bits = toybuf+1, 0, 19);
        for (i=0; i<hufflen; i++) bits[hufflen_order[i]] = bitbuf_get(bb, 3);
        len2huff(h2, bits, 19);

        // Use that tree to read in the literal and distance bit lengths
        for (i = 0; i < litlen + distlen;) {
          int sym = huff_and_puff(bb, h2);

          // 0-15 are literals, 16 = repeat previous code 3-6 times,
          // 17 = 3-10 zeroes (3 bit), 18 = 11-138 zeroes (7 bit)
          if (sym < 16) bits[i++] = sym;
          else {
            int len = sym & 2;

            len = bitbuf_get(bb, sym-14+len+(len>>1)) + 3 + (len<<2);
            memset(bits+i, bits[i-1] * !(sym&3), len);
            i += len;
          }
        }
        if (i > litlen+distlen) error_exit("bad tree");

        len2huff(lithuff = h2, bits, litlen);
        len2huff(disthuff = ((struct huff *)toybuf)+2, bits+litlen, distlen);

      // Static huffman codes
      } else {
        lithuff = TT.fixlithuff;
        disthuff = TT.fixdisthuff;
      }

      // Use huffman tables to decode block of compressed symbols
      for (;;) {
        int sym = huff_and_puff(bb, lithuff);

        // Literal?
        if (sym < 256) output_byte(sym);

        // Copy range?
        else if (sym > 256) {
          int len, dist;

          sym -= 257;
          len = TT.lenbase[sym] + bitbuf_get(bb, TT.lenbits[sym]);
          sym = huff_and_puff(bb, disthuff);
          dist = TT.distbase[sym] + bitbuf_get(bb, TT.distbits[sym]);
          sym = TT.pos & 32767;

          while (len--) output_byte(TT.data[(TT.pos-dist) & 32767]);

        // End of block
        } else break;
      }
    }

    // Was that the last block?
    if (final) break;
  }

  if (TT.pos & 32767) {
    xwrite(TT.outfd, TT.data, TT.pos & 32767);
    if (TT.crcfunc) TT.crcfunc(TT.data, TT.pos & 32767);
  }
}

// Deflate from TT.infd to bitbuf
// For deflate, TT.len = input read, TT.pos = input consumed
static void deflate(struct bitbuf *bb)
{
  char *data = TT.data;
  int len, final = 0;

  TT.crc = ~0;

  while (!final) {
    // Read next half-window of data if we haven't hit EOF yet.
    len = readall(TT.infd, data+(TT.len&32768), 32768);
    if (len < 0) perror_exit("read"); // todo: add filename
    if (len != 32768) final++;
    if (TT.crcfunc) TT.crcfunc(data+(TT.len&32768), len);
    // TT.len += len;  crcfunc advances len

    // store block as literal
    bitbuf_put(bb, final, 1);
    bitbuf_put(bb, 0, 1);

    bitbuf_put(bb, 0, (8-bb->bitpos)&7);
    bitbuf_put(bb, len, 16);
    bitbuf_put(bb, 0xffff & ~len, 16);

    // repeat until spanked
    while (TT.pos != TT.len) {
      unsigned pos = TT.pos & 65535;

      bitbuf_put(bb, data[pos], 8);

      // need to refill buffer?
      if (!(32767 & ++TT.pos) && !final) break;
    }
  }
  bitbuf_flush(bb);
}

// Allocate memory for deflate/inflate.
static void init_deflate(int compress)
{
  int i, n = 1;

  // compress needs 64k data and 32k each for hashhead and hashchain.
  // decompress just needs 32k data.
  TT.data = xmalloc(32768*(compress ? 4 : 1));
  if (compress) {
    TT.hashhead = (unsigned short *)(TT.data + 65536);
    TT.hashchain = (unsigned short *)(TT.data + 65536 + 32768);
  }

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

  // Init fixed huffman tables
  for (i=0; i<288; i++) toybuf[i] = 8 + (i>143) - ((i>255)<<1) + (i>279);
  len2huff(TT.fixlithuff = ((struct huff *)toybuf)+3, toybuf, 288);
  memset(toybuf, 5, 30);
  len2huff(TT.fixdisthuff = ((struct huff *)toybuf)+4, toybuf, 30);
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

void gzip_crc(char *data, int len)
{
  int i;
  unsigned crc, *crc_table = (unsigned *)(toybuf+sizeof(toybuf)-1024);

  crc = TT.crc;
  for (i=0; i<len; i++) crc = crc_table[(crc^data[i])&0xff] ^ (crc>>8);
  TT.crc = crc;
  TT.len += len;
}

static void do_gzip(int fd, char *name)
{
  struct bitbuf *bb = bitbuf_init(1, sizeof(toybuf));

  // Header from RFC 1952 section 2.2:
  // 2 ID bytes (1F, 8b), gzip method byte (8=deflate), FLAG byte (none),
  // 4 byte MTIME (zeroed), Extra Flags (2=maximum compression),
  // Operating System (FF=unknown)
 
  TT.infd = fd;
  xwrite(bb->fd, "\x1f\x8b\x08\0\0\0\0\0\x02\xff", 10);

  // Use last 1k of toybuf for little endian crc table
  crc_init((unsigned *)(toybuf+sizeof(toybuf)-1024), 1);
  TT.crcfunc = gzip_crc;

  deflate(bb);

  // tail: crc32, len32

  bitbuf_put(bb, 0, (8-bb->bitpos)&7);
  bitbuf_put(bb, ~TT.crc, 32);
  bitbuf_put(bb, TT.len, 32);

  bitbuf_flush(bb);
  free(bb);
}

static void do_zcat(int fd, char *name)
{
  struct bitbuf *bb = bitbuf_init(fd, sizeof(toybuf));

  if (!is_gzip(bb)) error_exit("not gzip");
  TT.outfd = 1;

  // Use last 1k of toybuf for little endian crc table
  crc_init((unsigned *)(toybuf+sizeof(toybuf)-1024), 1);
  TT.crcfunc = gzip_crc;

  inflate(bb);

  // tail: crc32, len32

  bitbuf_skip(bb, (8-bb->bitpos)&7);
  if (~TT.crc != bitbuf_get(bb, 32) || TT.len != bitbuf_get(bb, 32))
    error_exit("bad crc");
  free(bb);
}

// Parse many different kinds of command line argument:

void compress_main(void)
{
  // todo: this
  printf("hello world");
}
