/* gzip.c - deflate and inflate code rolled into a ball.
 *
 * Copyright 2009 Szabolcs Nagy <nszabolcs@gmail.com>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/gzip.html
 * And RFCs 1950, 1951, and 1952

USE_GZIP(NEWTOY(gzip, "qvcdrgzp", TOYFLAG_USR|TOYFLAG_BIN))

config GZIP
  bool "gzip"
  default n
  help
    usage: gzip [-qvcdrgzp] FILE

    Transitional gzip, needs work. Combines gzip, zlib, and pkzip.

    -q quiet (default)\n
    -v verbose\n
    -c compress (default)\n
    -d decompress\n
    -r raw (default)\n
    -g gzip\n
    -z zlib\n
    -p pkzip\n
*/

#define FOR_gzip
#include "toys.h"

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;

/* deflate and inflate return values */
enum {
  FlateOk  = 0,
  FlateErr = -1,
  FlateIn  = -2,
  FlateOut = -3
};

typedef struct {
  int nin;
  int nout;
  uchar *in;
  uchar *out;
  char *err;
  void *state;
} FlateStream;

int deflate(FlateStream *s);
int inflate(FlateStream *s);

uint adler32(uchar *p, int n, uint adler);
void crc32init(void);
uint crc32(uchar *p, int n, uint crc);

uint crctab[256];

void crc32init(void)
{
  static const uint poly = 0xedb88320;
  int i,j;

  for (i = 0; i < 256; ++i) {
    uint crc = i;

    for (j = 0; j < 8; j++) {
      if (crc & 1) crc = (crc >> 1) ^ poly;
      else crc >>= 1;
    }
    crctab[i] = crc;
  }
}

uint crc32(uchar *p, int n, uint crc)
{
  uchar *ep = p + n;

  crc ^= 0xffffffff;
  while (p < ep) crc = crctab[(crc & 0xff) ^ *p++] ^ (crc >> 8);

  return crc ^ 0xffffffff;
}

enum {
  AdlerBase = 65521, /* largest 16bit prime */
  AdlerN    = 5552   /* max iters before 32bit overflow */
};

uint adler32(uchar *p, int n, uint adler)
{
  uint s1 = adler & 0xffff;
  uint s2 = (adler >> 16) & 0xffff;
  uchar *ep;
  int k;

  for (; n >= 16; n -= k) {
    k = n < AdlerN ? n : AdlerN;
    k &= ~0xf;
    for (ep = p + k; p < ep; p += 16) {
      s1 += p[0];
      s2 += s1;
      s1 += p[1];
      s2 += s1;
      s1 += p[2];
      s2 += s1;
      s1 += p[3];
      s2 += s1;
      s1 += p[4];
      s2 += s1;
      s1 += p[5];
      s2 += s1;
      s1 += p[6];
      s2 += s1;
      s1 += p[7];
      s2 += s1;
      s1 += p[8];
      s2 += s1;
      s1 += p[9];
      s2 += s1;
      s1 += p[10];
      s2 += s1;
      s1 += p[11];
      s2 += s1;
      s1 += p[12];
      s2 += s1;
      s1 += p[13];
      s2 += s1;
      s1 += p[14];
      s2 += s1;
      s1 += p[15];
      s2 += s1;
    }
    s1 %= AdlerBase;
    s2 %= AdlerBase;
  }
  if (n) {
    for (ep = p + n; p < ep; p++) {
      s1 += p[0];
      s2 += s1;
    }
    s1 %= AdlerBase;
    s2 %= AdlerBase;
  }
  return (s2 << 16) + s1;
}

enum {
  CodeBits        = 16,  /* max number of bits in a code + 1 */
  LitlenTableBits = 9,   /* litlen code bits used in lookup table */
  DistTableBits   = 6,   /* dist code bits used in lookup table */
  ClenTableBits   = 6,   /* clen code bits used in lookup table */
  TableBits       = LitlenTableBits, /* log2(lookup table size) */
  Nlit            = 256, /* number of lit codes */
  Nlen            = 29,  /* number of len codes */
  Nlitlen         = Nlit+Nlen+3, /* litlen codes + block end + 2 unused */
  Ndist           = 30,  /* number of distance codes */
  Nclen           = 19,  /* number of code length codes */

  EOB         = 256,     /* end of block symbol */
  MinMatch    = 3,       /* min match length */
  MaxMatch    = 258,     /* max match length */
  WinSize     = 1 << 15, /* sliding window size */

  MaxChainLen = 256,     /* max length of hash chain */
  HashBits    = 13,
  HashSize    = 1 << HashBits, /* hash table size */
  BigDist     = 1 << 12, /* max match distance for short match length */
  MaxDist     = WinSize,
  BlockSize   = 1 << 15, /* TODO */
  SrcSize     = 2*WinSize + MaxMatch,
  DstSize     = BlockSize + MaxMatch + 6, /* worst case compressed block size */
  LzSize      = 1 << 13, /* lz buffer size */
  LzGuard     = LzSize - 2,
  LzLitFlag   = 1 << 15  /* marks literal run length in lz buffer */
};

/* states */
enum {
  BlockHead,
  UncompressedBlock,
  CopyUncompressed,
  FixedHuff,
  DynamicHuff,
  DynamicHuffClen,
  DynamicHuffLitlenDist,
  DynamicHuffContinue,
  DecodeBlock,
  DecodeBlockLenBits,
  DecodeBlockDist,
  DecodeBlockDistBits,
  DecodeBlockCopy
};

typedef struct {
  short len;  /* code length */
  ushort sym; /* symbol */
} Entry;

/* huffman code tree */
typedef struct {
  Entry table[1 << TableBits]; /* prefix lookup table */
  uint nbits;             /* prefix length (table size is 1 << nbits) */
  uint sum;               /* full codes in table: sum(count[0..nbits]) */
  ushort count[CodeBits]; /* number of codes with given length */
  ushort symbol[Nlitlen]; /* symbols ordered by code length (lexic.) */
} Huff;

typedef struct {
  uchar *src;  /* input buffer pointer */
  uchar *srcend;

  uint bits;
  uint nbits;

  uchar win[WinSize]; /* output window */
  uint pos;    /* window pos */
  uint posout; /* used for flushing win */

  int state;   /* decode state */
  int final;   /* last block flag */
  char *err;   /* TODO: error message */

  /* for decoding dynamic code trees in inflate() */
  int nlit;
  int ndist;
  int nclen;   /* also used in decode_block() */
  int lenpos;  /* also used in decode_block() */
  uchar lens[Nlitlen + Ndist];

  int fixed;   /* fixed code tree flag */
  Huff lhuff;  /* dynamic lit/len huffman code tree */
  Huff dhuff;  /* dynamic distance huffman code tree */
} DecodeState;

/* TODO: globals.. initialization is not thread safe */
static Huff lhuff; /* fixed lit/len huffman code tree */
static Huff dhuff; /* fixed distance huffman code tree */

/* base offset and extra bits tables */
static uchar lenbits[Nlen] = {
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static ushort lenbase[Nlen] = {
  3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static uchar distbits[Ndist] = {
  0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static ushort distbase[Ndist] = {
  1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

/* ordering of code lengths */
static uchar clenorder[Nclen] = {
  16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* TODO: this or normal inc + reverse() */
/* increment bitwise reversed n (msb is bit 0, lsb is bit len-1) */
static uint revinc(uint n, uint len) {
  uint i = 1 << (len - 1);

  while (n & i) i >>= 1;
  if (i) {
    n &= i - 1;
    n |= i;
  } else n = 0;

  return n;
}

/* build huffman code tree from code lengths (each should be < CodeBits) */
static int build_huff(Huff *huff, uchar *lens, uint n, uint nbits)
{
  int offs[CodeBits];
  int left;
  uint i, c, sum, code, len, min, max;
  ushort *count = huff->count;
  ushort *symbol = huff->symbol;
  Entry *table = huff->table;
  Entry entry;

  /* count code lengths */
  for (i = 0; i < CodeBits; i++) count[i] = 0;
  for (i = 0; i < n; i++) count[lens[i]]++;
  if (count[0] == n) {
    huff->nbits = table[0].len = 0;
    return 0;
  }
  count[0] = 0;

  /* bound code lengths, force nbits to be within the bounds */
  for (max = CodeBits - 1; max > 0; max--) if (count[max] != 0) break;
  if (nbits > max) nbits = max;
  for (min = 1; min < CodeBits; min++) if (count[min] != 0) break;
  if (nbits < min) {
    nbits = min;
    if (nbits > TableBits) return -1;
  }
  huff->nbits = nbits;

  /* check if length is over-subscribed or incomplete */
  for (left = 1 << min, i = min; i <= max; left <<= 1, i++) {
    left -= count[i];
    /* left < 0: over-subscribed, left > 0: incomplete */
    if (left < 0) return -1;
  }

  for (sum = 0, i = 0; i <= max; i++) {
    offs[i] = sum;
    sum += count[i];
  }
  /* needed for decoding codes longer than nbits */
  if (nbits < max) huff->sum = offs[nbits + 1];

  /* sort symbols by code length (lexicographic order) */
  for (i = 0; i < n; i++) if (lens[i]) symbol[offs[lens[i]]++] = i;

  /* lookup table for decoding nbits from input.. */
  for (i = 0; i < 1 << nbits; i++) table[i].len = table[i].sym = 0;
  code = 0;
  /* ..if code is at most nbits (bits are in reverse order, sigh..) */
  for (len = min; len <= nbits; len++)
    for (c = count[len]; c > 0; c--) {
      entry.len = len;
      entry.sym = *symbol;
      for (i = code; i < 1 << nbits; i += 1 << len) table[i] = entry;
      /* next code */
      symbol++;
      code = revinc(code, len);
    }
  /* ..if code is longer than nbits: values for simple bitwise decode */
  for (i = 0; code; i++) {
    table[code].len = -1;
    table[code].sym = i << 1;
    code = revinc(code, nbits);
  }

  return 0;
}

/* fixed huffman code trees (should be done at compile time..) */
static void init_fixed_huffs(void)
{
  int i;
  uchar lens[Nlitlen];

  for (i = 0; i < 144; i++) lens[i] = 8;
  for (; i < 256; i++) lens[i] = 9;
  for (; i < 280; i++) lens[i] = 7;
  for (; i < Nlitlen; i++) lens[i] = 8;
  build_huff(&lhuff, lens, Nlitlen, 8);

  for (i = 0; i < Ndist; i++) lens[i] = 5;
  build_huff(&dhuff, lens, Ndist, 5);
}

/* fill *bits with n bits from *src */
static int fillbits_fast(uchar **src, uchar *srcend, uint *bits, uint *nbits,
  uint n)
{
  while (*nbits < n) {
    if (*src == srcend) return 0;
    *bits |= *(*src)++ << *nbits;
    *nbits += 8;
  }

  return 1;
}

/* get n bits from *bits */
static uint getbits_fast(uint *bits, uint *nbits, int n)
{
  uint k;

  k = *bits & ((1 << n) - 1);
  *bits >>= n;
  *nbits -= n;

  return k;
}

static int fillbits(DecodeState *s, uint n)
{
  return fillbits_fast(&s->src, s->srcend, &s->bits, &s->nbits, n);
}

static uint getbits(DecodeState *s, uint n)
{
  return getbits_fast(&s->bits, &s->nbits, n);
}

/* decode symbol bitwise if code is longer than huffbits */
static uint decode_symbol_long(DecodeState *s, Huff *huff, uint bits,
  uint nbits, int cur)
{
  int sum = huff->sum;
  uint huffbits = huff->nbits;
  ushort *count = huff->count + huffbits + 1;

  /* get bits if we are near the end */
  if (s->src + 2 >= s->srcend) {
    while (nbits < CodeBits - 1 && s->src < s->srcend) {
      bits |= *s->src++ << nbits;
      nbits += 8;
    }
    s->bits = bits;
    s->nbits = nbits;
  }
  bits >>= huffbits;
  nbits -= huffbits;
  for (;;) {
    if (!nbits--) {
      if (s->src == s->srcend) return FlateIn;
      bits = *s->src++;
      nbits = 7;
    }
    cur |= bits & 1;
    bits >>= 1;
    sum += *count;
    cur -= *count;
    if (cur < 0) break;
    cur <<= 1;
    count++;
    if (count == huff->count + CodeBits)
      return s->err = "symbol decoding failed.", FlateErr;
  }
  s->bits = bits;
  s->nbits = nbits;

  return huff->symbol[sum + cur];
}

/* decode a symbol from stream with huff code */
static uint decode_symbol(DecodeState *s, Huff *huff)
{
  uint huffbits = huff->nbits;
  uint nbits = s->nbits;
  uint bits = s->bits;
  uint mask = (1 << huffbits) - 1;
  Entry entry;

  /* get enough bits efficiently */
  if (nbits < huffbits) {
    uchar *src = s->src;

    if (src + 2 < s->srcend) {
      /* we assume huffbits <= 9 */
      bits |= *src++ << nbits;
      nbits += 8;
      bits |= *src++ << nbits;
      nbits += 8;
      bits |= *src++ << nbits;
      nbits += 8;
      s->src = src;
    } else /* rare */
      do {
        if (s->src == s->srcend) {
          entry = huff->table[bits & mask];
          if (entry.len > 0 && entry.len <= nbits) {
            s->bits = bits >> entry.len;
            s->nbits = nbits - entry.len;
            return entry.sym;
          }
          s->bits = bits;
          s->nbits = nbits;
          return FlateIn;
        }
        bits |= *s->src++ << nbits;
        nbits += 8;
      } while (nbits < huffbits);
  }
  /* decode bits */
  entry = huff->table[bits & mask];
  if (entry.len > 0) {
    s->bits = bits >> entry.len;
    s->nbits = nbits - entry.len;
    return entry.sym;
  } else if (entry.len == 0)
    return s->err = "symbol decoding failed.", FlateErr;
  return decode_symbol_long(s, huff, bits, nbits, entry.sym);
}

/* decode a block of data from stream with trees */
static int decode_block(DecodeState *s, Huff *lhuff, Huff *dhuff)
{
  uchar *win = s->win;
  uint pos = s->pos;
  uint sym = s->nclen;
  uint len = s->lenpos;
  uint dist = s->nclen;

  switch (s->state) {
  case DecodeBlock:
  for (;;) {
    sym = decode_symbol(s, lhuff);
    if (sym < 256) {
      win[pos++] = sym;
      if (pos == WinSize) {
        s->pos = WinSize;
        s->state = DecodeBlock;
        return FlateOut;
      }
    } else if (sym > 256) {
      sym -= 257;
      if (sym >= Nlen) {
        s->pos = pos;
        s->state = DecodeBlock;
        if (sym + 257 == (uint)FlateIn) return FlateIn;
        return FlateErr;
      }
  case DecodeBlockLenBits:
      if (!fillbits_fast(&s->src, s->srcend, &s->bits, &s->nbits, lenbits[sym]))
      {
        s->nclen = sym; /* using nclen to store sym */
        s->pos = pos;
        s->state = DecodeBlockLenBits;
        return FlateIn;
      }
      len = lenbase[sym] + getbits_fast(&s->bits, &s->nbits, lenbits[sym]);
  case DecodeBlockDist:
      sym = decode_symbol(s, dhuff);
      if (sym == (uint)FlateIn) {
        s->pos = pos;
        s->lenpos = len;
        s->state = DecodeBlockDist;
        return FlateIn;
      }
      if (sym >= Ndist) return FlateErr;
  case DecodeBlockDistBits:
      if (!fillbits_fast(&s->src, s->srcend, &s->bits, &s->nbits, distbits[sym])) {
        s->nclen = sym; /* using nclen to store sym */
        s->pos = pos;
        s->lenpos = len;
        s->state = DecodeBlockDistBits;
        return FlateIn;
      }
      dist = distbase[sym] + getbits_fast(&s->bits, &s->nbits, distbits[sym]);
      /* copy match, loop unroll in common case */
      if (pos + len < WinSize) {
        /* lenbase[sym] >= 3 */
        do {
          win[pos] = win[(pos - dist) % WinSize];
          pos++;
          win[pos] = win[(pos - dist) % WinSize];
          pos++;
          win[pos] = win[(pos - dist) % WinSize];
          pos++;
          len -= 3;
        } while (len >= 3);
        if (len--) {
          win[pos] = win[(pos - dist) % WinSize];
          pos++;
          if (len) {
            win[pos] = win[(pos - dist) % WinSize];
            pos++;
          }
        }
      } else { /* rare */
  case DecodeBlockCopy:
        while (len--) {
          win[pos] = win[(pos - dist) % WinSize];
          pos++;
          if (pos == WinSize) {
            s->pos = WinSize;
            s->lenpos = len;
            s->nclen = dist; /* using nclen to store dist */
            s->state = DecodeBlockCopy;
            return FlateOut;
          }
        }
      }
    } else { /* EOB: sym == 256 */
      s->pos = pos;
      return FlateOk;
    }
  } /* for (;;) */
  } /* switch () */
  return s->err = "corrupted state.", FlateErr;
}

/* inflate state machine (decodes s->src into s->win) */
static int inflate_state(DecodeState *s)
{
  int n;

  if (s->posout) return FlateOut;
  for (;;) {
    switch (s->state) {
    case BlockHead:
      if (s->final) {
        if (s->pos) return FlateOut;
        else return FlateOk;
      }
      if (!fillbits(s, 3)) return FlateIn;
      s->final = getbits(s, 1);
      n = getbits(s, 2);
      if (n == 0) s->state = UncompressedBlock;
      else if (n == 1) s->state = FixedHuff;
      else if (n == 2) s->state = DynamicHuff;
      else return s->err = "corrupt block header.", FlateErr;
      break;
    case UncompressedBlock:
      /* start block on a byte boundary */
      s->bits >>= s->nbits & 7;
      s->nbits &= ~7;
      if (!fillbits(s, 32)) return FlateIn;
      s->lenpos = getbits(s, 16);
      n = getbits(s, 16);
      if (s->lenpos != (~n & 0xffff))
        return s->err = "corrupt uncompressed length.", FlateErr;
      s->state = CopyUncompressed;
    case CopyUncompressed:
      /* TODO: untested, slow, memcpy etc */
      /* s->nbits should be 0 here */
      while (s->lenpos) {
        if (s->src == s->srcend) return FlateIn;
        s->lenpos--;
        s->win[s->pos++] = *s->src++;
        if (s->pos == WinSize) return FlateOut;
      }
      s->state = BlockHead;
      break;
    case FixedHuff:
      s->fixed = 1;
      s->state = DecodeBlock;
      break;
    case DynamicHuff:
      /* decode dynamic huffman code trees */
      if (!fillbits(s, 14)) return FlateIn;
      s->nlit = 257 + getbits(s, 5);
      s->ndist = 1 + getbits(s, 5);
      s->nclen = 4 + getbits(s, 4);
      if (s->nlit > Nlitlen || s->ndist > Ndist)
        return s->err = "corrupt code tree.", FlateErr;
      /* build code length tree */
      for (n = 0; n < Nclen; n++) s->lens[n] = 0;
      s->fixed = 0;
      s->state = DynamicHuffClen;
      s->lenpos = 0;
    case DynamicHuffClen:
      for (n = s->lenpos; n < s->nclen; n++)
        if (fillbits(s, 3)) s->lens[clenorder[n]] = getbits(s, 3);
        else {
          s->lenpos = n;
          return FlateIn;
        }
      /* using lhuff for code length huff code */
      if (build_huff(&s->lhuff, s->lens, Nclen, ClenTableBits) < 0)
        return s->err = "building clen tree failed.", FlateErr;
      s->state = DynamicHuffLitlenDist;
      s->lenpos = 0;
    case DynamicHuffLitlenDist:
      /* decode code lengths for the dynamic trees */
      for (n = s->lenpos; n < s->nlit + s->ndist; ) {
        uint sym = decode_symbol(s, &s->lhuff);
        uint len;
        uchar c;

        if (sym < 16) {
          s->lens[n++] = sym;
          continue;
        } else if (sym == (uint)FlateIn) {
          s->lenpos = n;
          return FlateIn;
    case DynamicHuffContinue:
          n = s->lenpos;
          sym = s->nclen;
          s->state = DynamicHuffLitlenDist;
        }
        if (!fillbits(s, 7)) {
          /* TODO: 7 is too much when an almost empty block is at the end */
          if (sym == (uint)FlateErr)
            return FlateErr;
          s->nclen = sym;
          s->lenpos = n;
          s->state = DynamicHuffContinue;
          return FlateIn;
        }
        /* TODO: bound check s->lens */
        if (sym == 16) {
          /* copy previous code length 3-6 times */
          c = s->lens[n - 1];
          for (len = 3 + getbits(s, 2); len; len--)
            s->lens[n++] = c;
        } else if (sym == 17) {
          /* repeat 0 for 3-10 times */
          for (len = 3 + getbits(s, 3); len; len--) s->lens[n++] = 0;
        } else if (sym == 18) {
          /* repeat 0 for 11-138 times */
          for (len = 11 + getbits(s, 7); len; len--) s->lens[n++] = 0;
        } else return s->err = "corrupt code tree.", FlateErr;
      }
      /* build dynamic huffman code trees */
      if (build_huff(&s->lhuff, s->lens, s->nlit, LitlenTableBits) < 0)
        return s->err = "building litlen tree failed.", FlateErr;
      if (build_huff(&s->dhuff, s->lens + s->nlit, s->ndist, DistTableBits) < 0)
        return s->err = "building dist tree failed.", FlateErr;
      s->state = DecodeBlock;
    case DecodeBlock:
    case DecodeBlockLenBits:
    case DecodeBlockDist:
    case DecodeBlockDistBits:
    case DecodeBlockCopy:
      n = decode_block(s, s->fixed ? &lhuff : &s->lhuff, s->fixed ? &dhuff : &s->dhuff);
      if (n != FlateOk)
        return n;
      s->state = BlockHead;
      break;
    default:
      return s->err = "corrupt internal state.", FlateErr;
    }
  }
}

static DecodeState *alloc_decode_state(void)
{
  DecodeState *s = malloc(sizeof(DecodeState));

  if (s) {
    s->final = s->pos = s->posout = s->bits = s->nbits = 0;
    s->state = BlockHead;
    s->src = s->srcend = 0;
    s->err = 0;
    /* TODO: globals.. */
    if (lhuff.nbits == 0) init_fixed_huffs();
  }
  return s;
}


/* extern */

int inflate(FlateStream *stream)
{
  DecodeState *s = stream->state;
  int n;

  if (stream->err) {
    if (s) {
      free(s);
      stream->state = 0;
    }
    return FlateErr;
  }
  if (!s) {
    s = stream->state = alloc_decode_state();
    if (!s) return stream->err = "no mem.", FlateErr;
  }
  if (stream->nin) {
    s->src = stream->in;
    s->srcend = s->src + stream->nin;
    stream->nin = 0;
  }
  n = inflate_state(s);
  if (n == FlateOut) {
    if (s->pos < stream->nout) stream->nout = s->pos;
    memcpy(stream->out, s->win + s->posout, stream->nout);
    s->pos -= stream->nout;
    if (s->pos) s->posout += stream->nout;
    else s->posout = 0;
  }
  if (n == FlateOk || n == FlateErr) {
    if (s->nbits || s->src < s->srcend) {
      s->nbits /= 8;
      stream->in = s->src - s->nbits;
      stream->nin = s->srcend - s->src + s->nbits;
    }
    stream->err = s->err;
    free(s);
    stream->state = 0;
  }
  return n;
}

typedef struct {
  ushort dist;
  ushort len;
} Match;

typedef struct {
  ushort n;
  ushort bits;
} LzCode;

typedef struct {
  int pos;               /* position in input src */
  int startpos;          /* block start pos in input src */
  int endpos;            /* end of available bytes in src */
  int skip;              /* skipped hash chain updates (until next iter) */
  Match prevm;           /* previous (deferred) match */
  int state;             /* prev return value */
  int eof;               /* end of input */
  uchar *in;             /* input data (not yet in src) */
  uchar *inend;
  uint bits;             /* for output */
  int nbits;             /* for output */
  uchar *dst;            /* compressed output (position in dstbuf) */
  uchar *dstbegin;       /* start position of unflushed data in dstbuf */
  LzCode *lz;            /* current pos in lzbuf */
  int nlit;              /* literal run length in lzbuf */
  ushort head[HashSize]; /* position of hash chain heads */
  ushort chain[WinSize]; /* hash chain */
  ushort lfreq[Nlitlen];
  ushort dfreq[Ndist];
  uchar src[SrcSize];    /* input buf */
  uchar dstbuf[DstSize];
  LzCode lzbuf[LzSize];  /* literal run length, match len, match dist */
} State;

static uchar fixllen[Nlitlen]; /* fixed lit/len huffman code tree */
static ushort fixlcode[Nlitlen];
static uchar fixdlen[Ndist];   /* fixed distance huffman code tree */
static ushort fixdcode[Ndist];

static uint revcode(uint c, int n)
{
  int i;
  uint r = 0;

  for (i = 0; i < n; i++) {
    r = (r << 1) | (c & 1);
    c >>= 1;
  }
  return r;
}

/* build huffman code tree from code lengths */
static void huffcodes(ushort *codes, const uchar *lens, int n)
{
  int c[CodeBits];
  int i, code, count;

  /* count code lengths and calc first code for each length */
  for (i = 0; i < CodeBits; i++) c[i] = 0;
  for (i = 0; i < n; i++) c[lens[i]]++;
  for (code = 0, i = 1; i < CodeBits; i++) {
    count = c[i];
    c[i] = code;
    code += count;
    if (code > (1 << i)) abort(); /* over-subscribed */
    code <<= 1;
  }
  if (code < (1 << i))
    /* incomplete */;

  for (i = 0; i < n; i++)
    if (lens[i]) codes[i] = revcode(c[lens[i]]++, lens[i]);
    else codes[i] = 0;
}

static int heapparent(int n) {return (n - 2)/4 * 2;}
static int heapchild(int n)  {return 2 * n + 2;}

static int heappush(int *heap, int len, int w, int n)
{
  int p, c, tmp;

  c = len;
  heap[len++] = n;
  heap[len++] = w;
  while (c > 0) {
    p = heapparent(c);
    if (heap[c+1] < heap[p+1]) {
      tmp = heap[c]; heap[c] = heap[p]; heap[p] = tmp;
      tmp = heap[c+1]; heap[c+1] = heap[p+1]; heap[p+1] = tmp;
      c = p;
    } else break;
  }
  return len;
}

static int heappop(int *heap, int len, int *w, int *n)
{
  int p, c, tmp;

  *n = heap[0];
  *w = heap[1];
  heap[1] = heap[--len];
  heap[0] = heap[--len];
  p = 0;
  for (;;) {
    c = heapchild(p);
    if (c >= len) break;
    if (c+2 < len && heap[c+3] < heap[c+1]) c += 2;
    if (heap[p+1] > heap[c+1]) {
      tmp = heap[p]; heap[p] = heap[c]; heap[c] = tmp;
      tmp = heap[p+1]; heap[p+1] = heap[c+1]; heap[c+1] = tmp;
    } else break;
    p = c;
  }
  return len;
}

/* symbol frequencies -> code lengths (limited to 255) */
static void hufflens(uchar *lens, ushort *freqs, int nsym, int limit)
{
  /* 2 <= nsym <= Nlitlen, log(nsym) <= limit <= CodeBits-1 */
  int parent[2*Nlitlen-1];
  int count[CodeBits];
  int heap[2*Nlitlen];
  int n, len, top, overflow;
  int i, j;
  int wi, wj;

  for (n = 0; n < limit+1; n++) count[n] = 0;
  for (len = n = 0; n < nsym; n++)
    if (freqs[n] > 0) len = heappush(heap, len, freqs[n], n);
    else lens[n] = 0;
  /* deflate: fewer than two symbols: add new */
  for (n = 0; len < 4; n++)
    if (freqs[n] == 0) len = heappush(heap, len, ++freqs[n], n);
  /* build code tree */
  top = len;
  for (n = nsym; len > 2; n++) {
    len = heappop(heap, len, &wi, &i);
    len = heappop(heap, len, &wj, &j);
    parent[i] = n;
    parent[j] = n;
    len = heappush(heap, len, wi + wj, n);
    /* keep an ordered list of nodes at the end */
    heap[len+1] = i;
    heap[len] = j;
  }
  /* calc code lengths (deflate: with limit) */
  overflow = 0;
  parent[--n] = 0;
  for (i = 2; i < top; i++) {
    n = heap[i];
    if (n >= nsym) {
      /* overwrite parent index with length */
      parent[n] = parent[parent[n]] + 1;
      if (parent[n] > limit) overflow++;
    } else {
      lens[n] = parent[parent[n]] + 1;
      if (lens[n] > limit) {
        lens[n] = limit;
        overflow++;
      }
      count[lens[n]]++;
    }
  }
  if (overflow == 0) return;
  /* modify code tree to fix overflow (from zlib) */
  while (overflow > 0) {
    for (n = limit-1; count[n] == 0; n--);
    count[n]--;
    count[n+1] += 2;
    count[limit]--;
    overflow -= 2;
  }
  for (len = limit; len > 0; len--)
    for (i = count[len]; i > 0;) {
      n = heap[--top];
      if (n < nsym) {
        lens[n] = len;
        i--;
      }
    }
}

/* output n (<= 16) bits */
static void putbits(State *s, uint bits, int n)
{
  s->bits |= bits << s->nbits;
  s->nbits += n;
  while (s->nbits >= 8) {
    *s->dst++ = s->bits & 0xff;
    s->bits >>= 8;
    s->nbits -= 8;
  }
}

/* run length encode literal and dist code lengths into codes and extra */
static int clencodes(uchar *codes, uchar *extra, uchar *llen, int nlit,
  uchar *dlen, int ndist)
{
  int i, c, r, rr;
  int n = 0;

  for (i = 0; i < nlit; i++) codes[i] = llen[i];
  for (i = 0; i < ndist; i++) codes[nlit + i] = dlen[i];
  for (i = 0; i < nlit + ndist;) {
    c = codes[i];
    for (r = 1; i + r < nlit + ndist && codes[i + r] == c; r++);
    i += r;
    if (c == 0) {
      while (r >= 11) {
        rr = r > 138 ? 138 : r;
        codes[n] = 18;
        extra[n++] = rr - 11;
        r -= rr;
      }
      if (r >= 3) {
        codes[n] = 17;
        extra[n++] = r - 3;
        r = 0;
      }
    }
    while (r--) {
      codes[n++] = c;
      while (r >= 3) {
        rr = r > 6 ? 6 : r;
        codes[n] = 16;
        extra[n++] = rr - 3;
        r -= rr;
      }
    }
  }
  return n;
}

/* compress block data into s->dstbuf using given codes */
static void putblock(State *s, ushort *lcode, uchar *llen, ushort *dcode,
  uchar *dlen)
{
  int n;
  LzCode *lz;
  uchar *p;

  for (lz = s->lzbuf, p = s->src + s->startpos; lz != s->lz; lz++) {
    if (lz->bits & LzLitFlag) {
      for (n = lz->n; n > 0; n--, p++) putbits(s, lcode[*p], llen[*p]);
    } else {
      p += lenbase[lz->n] + lz->bits;
      putbits(s, lcode[Nlit + lz->n + 1], llen[Nlit + lz->n + 1]);
      putbits(s, lz->bits, lenbits[lz->n]);
      lz++;
      putbits(s, dcode[lz->n], dlen[lz->n]);
      putbits(s, lz->bits, distbits[lz->n]);
    }
  }
  putbits(s, lcode[EOB], llen[EOB]);
}

/* build code trees and select dynamic/fixed/uncompressed block compression */
static void deflate_block(State *s)
{
  uchar codes[Nlitlen + Ndist], extra[Nlitlen + Ndist];
  uchar llen[Nlitlen], dlen[Ndist], clen[Nclen];
  ushort cfreq[Nclen];
  /* freq can be overwritten by code */
  ushort *lcode = s->lfreq, *dcode = s->dfreq, *ccode = cfreq;
  int i, c, n, ncodes;
  int nlit, ndist, nclen;
  LzCode *lz;
  uchar *p;
  int dynsize, fixsize, uncsize;
  int blocklen = s->pos - s->startpos;
/* int dyntree; */

  /* calc dynamic codes */
  hufflens(llen, s->lfreq, Nlitlen, CodeBits-1);
  hufflens(dlen, s->dfreq, Ndist, CodeBits-1);
  huffcodes(lcode, llen, Nlitlen);
  huffcodes(dcode, dlen, Ndist);
  for (nlit = Nlitlen; nlit > Nlit && llen[nlit-1] == 0; nlit--);
  for (ndist = Ndist; ndist > 1 && dlen[ndist-1] == 0; ndist--);
  ncodes = clencodes(codes, extra, llen, nlit, dlen, ndist);
  memset(cfreq, 0, sizeof(cfreq));
  for (i = 0; i < ncodes; i++) cfreq[codes[i]]++;
  hufflens(clen, cfreq, Nclen, 7);
  huffcodes(ccode, clen, Nclen);
  for (nclen = Nclen; nclen > 4 && clen[clenorder[nclen-1]] == 0; nclen--);

  /* calc compressed size */
  uncsize = 3 + 16 + 8 * blocklen + (16 - 3 - s->nbits) % 8; /* byte aligned */
  fixsize = 3;
  dynsize = 3 + 5 + 5 + 4 + 3 * nclen;
  for (i = 0; i < ncodes; i++) {
    c = codes[i];
    dynsize += clen[c];
    if (c == 16) dynsize += 2;
    if (c == 17) dynsize += 3;
    if (c == 18) dynsize += 7;
  }
/* dyntree = dynsize - 3; */
  for (lz = s->lzbuf, p = s->src + s->startpos; lz != s->lz; lz++) {
    if (lz->bits & LzLitFlag) {
      for (n = lz->n; n > 0; n--, p++) {
        fixsize += fixllen[*p];
        dynsize += llen[*p];
      }
    } else {
      p += lenbase[lz->n] + lz->bits;
      fixsize += fixllen[Nlit + lz->n + 1];
      dynsize += llen[Nlit + lz->n + 1];
      fixsize += lenbits[lz->n];
      dynsize += lenbits[lz->n];
      lz++;
      fixsize += fixdlen[lz->n];
      dynsize += dlen[lz->n];
      fixsize += distbits[lz->n];
      dynsize += distbits[lz->n];
    }
  }
  fixsize += fixllen[EOB];
  dynsize += llen[EOB];

  /* emit block */
  putbits(s, s->eof && s->pos == s->endpos, 1);
  if (dynsize < fixsize && dynsize < uncsize) {
    /* dynamic code */
    putbits(s, 2, 2);
    putbits(s, nlit - 257, 5);
    putbits(s, ndist - 1, 5);
    putbits(s, nclen - 4, 4);
    for (i = 0; i < nclen; i++) putbits(s, clen[clenorder[i]], 3);
    for (i = 0; i < ncodes; i++) {
      c = codes[i];
      putbits(s, ccode[c], clen[c]);
      if (c == 16) putbits(s, extra[i], 2);
      if (c == 17) putbits(s, extra[i], 3);
      if (c == 18) putbits(s, extra[i], 7);
    }
    putblock(s, lcode, llen, dcode, dlen);
  } else if (fixsize < uncsize) {
    /* fixed code */
    putbits(s, 1, 2);
    putblock(s, fixlcode, fixllen, fixdcode, fixdlen);
  } else {
    /* uncompressed */
    putbits(s, 0, 2);
    putbits(s, 0, 7); /* align to byte boundary */
    s->nbits = 0;
    putbits(s, blocklen, 16);
    putbits(s, ~blocklen & 0xffff, 16);
    memcpy(s->dst, s->src + s->startpos, blocklen);
    s->dst += blocklen;
  }
/*
fprintf(stderr, "blen:%d [%d,%d] lzlen:%d dynlen:%d (tree:%d rate:%.3f) fixlen:%d (rate:%.3f) unclen:%d (rate:%.3f)\n",
  blocklen, s->startpos, s->pos, s->lz - s->lzbuf, dynsize, dyntree, dynsize/(float)blocklen,
  fixsize, fixsize/(float)blocklen, uncsize, uncsize/(float)blocklen);
*/
}

/* find n in base */
static int bisect(ushort *base, int len, int n)
{
  int lo = 0;
  int hi = len;
  int k;

  while (lo < hi) {
    k = (lo + hi) / 2;
    if (n < base[k]) hi = k;
    else lo = k + 1;
  }
  return lo - 1;
}

/* add literal run length to lzbuf */
static void flushlit(State *s)
{
  if (s->nlit) {
    s->lz->bits = LzLitFlag;
    s->lz->n = s->nlit;
    s->lz++;
    s->nlit = 0;
  }
}

/* add match to lzbuf and update freq counts */
static void recordmatch(State *s, Match m)
{
  int n;

/*fprintf(stderr, "m %d %d\n", m.len, m.dist);*/
  flushlit(s);
  n = bisect(lenbase, Nlen, m.len);
  s->lz->n = n;
  s->lz->bits = m.len - lenbase[n];
  s->lz++;
  s->lfreq[Nlit + n + 1]++;
  n = bisect(distbase, Ndist, m.dist);
  s->lz->n = n;
  s->lz->bits = m.dist - distbase[n];
  s->lz++;
  s->dfreq[n]++;
}

/* update literal run length */
static void recordlit(State *s, int c)
{
/*fprintf(stderr, "l %c\n", c);*/
  s->nlit++;
  s->lfreq[c]++;
}

/* multiplicative hash (using a prime close to golden ratio * 2^32) */
static int gethash(uchar *p)
{
  return (0x9e3779b1 * ((p[0]<<16) + (p[1]<<8) + p[2]) >> (32 - HashBits))
         % HashSize;
}

/* update hash chain at the current position */
static int updatechain(State *s)
{
  int hash, next = 0, p = s->pos, i;

  if (s->endpos - p < MinMatch) p = s->endpos - MinMatch;
  for (i = s->pos - s->skip; i <= p; i++) {
    hash = gethash(s->src + i);
    next = s->head[hash];
    s->head[hash] = i;
    if (next >= i || i - next >= MaxDist) next = 0;
    s->chain[i % WinSize] = next;
  }
  s->skip = 0;

  return next;
}

/* find longest match, next position in the hash chain is given */
static Match getmatch(State *s, int next)
{
  Match m = {0, MinMatch-1};
  int len;
  int limit = s->pos - MaxDist;
  int chainlen = MaxChainLen;
  uchar *q;
  uchar *p = s->src + s->pos;
  uchar *end = p + MaxMatch;

  do {
    q = s->src + next;
/*fprintf(stderr,"match: next:%d pos:%d limit:%d\n", next, s->pos, limit);*/
    /* next match should be at least m.len+1 long */
    if (q[m.len] != p[m.len] || q[m.len-1] != p[m.len-1] || q[0] != p[0])
      continue;
    while (++p != end && *++q == *p);
    len = MaxMatch - (end - p);
    p -= len;
/*fprintf(stderr,"match: len:%d dist:%d\n", len, s->pos - next);*/
    if (len > m.len) {
      m.dist = s->pos - next;
      m.len = len;
      if (s->pos + len >= s->endpos) { /* TODO: overflow */
        m.len = s->endpos - s->pos;
        return m;
      }
      if (len == MaxMatch) return m;
    }
  } while ((next = s->chain[next % WinSize]) > limit && --chainlen);
  if (m.len < MinMatch || (m.len == MinMatch && m.dist > BigDist)) m.len = 0;

  return m;
}

static void startblock(State *s)
{
  s->startpos = s->pos;
  s->dst = s->dstbegin = s->dstbuf;
  s->lz = s->lzbuf;
  s->nlit = 0;
  memset(s->lfreq, 0, sizeof(s->lfreq));
  memset(s->dfreq, 0, sizeof(s->dfreq));
  s->lfreq[EOB]++;
}

static int shiftwin(State *s)
{
  int n;

  if (s->startpos < WinSize) return 0;
  memmove(s->src, s->src + WinSize, SrcSize - WinSize);
  for (n = 0; n < HashSize; n++)
    s->head[n] = s->head[n] > WinSize ? s->head[n] - WinSize : 0;
  for (n = 0; n < WinSize; n++)
    s->chain[n] = s->chain[n] > WinSize ? s->chain[n] - WinSize : 0;
  s->pos -= WinSize;
  s->startpos -= WinSize;
  s->endpos -= WinSize;

  return 1;
}

static int endblock(State *s)
{
  if ((s->pos >= 2*WinSize && !shiftwin(s))
      || s->pos - s->startpos >= BlockSize || s->lz - s->lzbuf >= LzGuard
      || (s->eof && s->pos == s->endpos))
  {
    /* deflate block */
    flushlit(s);
    if (s->prevm.len) s->pos--;
    deflate_block(s);
    if (s->eof && s->pos == s->endpos) putbits(s, 0, 7);

    return 1;
  }

  return 0;
}

static int fillsrc(State *s)
{
  int n, k;

  if (s->endpos < SrcSize && !s->eof) {
    n = SrcSize - s->endpos;
    k = s->inend - s->in;
    if (n > k) n = k;
    memcpy(s->src + s->endpos, s->in, n);
    s->in += n;
    s->endpos += n;
    if (s->endpos < SrcSize) return 0;
  }
  return 1;
}

static int calcguard(State *s) {
  int p = s->endpos - MaxMatch;
  int q = s->startpos + BlockSize;

  return p < q ? p : q;
}

/* deflate compress from s->src into s->dstbuf */
static int deflate_state(State *s)
{
  Match m;
  int next;
  int guard;

  if (s->state == FlateIn) s->eof = s->in == s->inend;
  else if (s->state == FlateOut) {
    if (s->dstbegin < s->dst) return (s->state = FlateOut);
    if (s->eof && s->pos == s->endpos) return (s->state = FlateOk);
    startblock(s);
    if (s->prevm.len) s->pos++;
  } else return s->state;

  guard = calcguard(s);
  for (;;) {
    if (s->pos >= guard || s->lz - s->lzbuf >= LzGuard) {
/*fprintf(stderr,"guard:%d pos:%d len:%d lzlen:%d end:%d start:%d nin:%d eof:%d\n", guard, s->pos, s->pos - s->startpos, s->lz - s->lzbuf, s->endpos, s->startpos, s->inend - s->in, s->eof);*/
      if (endblock(s)) return (s->state = FlateOut);
      if (!fillsrc(s)) return (s->state = FlateIn);
      guard = calcguard(s);
    }
    next = updatechain(s);
    if (next) m = getmatch(s, next);
    if (next && m.len > s->prevm.len) {
      if (s->prevm.len) recordlit(s, s->src[s->pos-1]);
      s->prevm = m;
    } else if (s->prevm.len) {
      recordmatch(s, s->prevm);
      s->skip = s->prevm.len - 2;
      s->prevm.len = 0;
      s->pos += s->skip;
    } else recordlit(s, s->src[s->pos]);
    s->pos++;
  }
}

/* alloc and init state */
static State *alloc_state(void)
{
  State *s = malloc(sizeof(State));
  int i;

  if (!s) return s;

  memset(s->chain, 0, sizeof(s->chain));
  memset(s->head, 0, sizeof(s->head));
  s->bits = s->nbits = 0;
  /* TODO: globals */
  if (fixllen[0] == 0) {
    for (i = 0; i < 144; i++) fixllen[i] = 8;
    for (; i < 256; i++) fixllen[i] = 9;
    for (; i < 280; i++) fixllen[i] = 7;
    for (; i < Nlitlen; i++) fixllen[i] = 8;
    for (i = 0; i < Ndist; i++) fixdlen[i] = 5;
    huffcodes(fixlcode, fixllen, Nlitlen);
    huffcodes(fixdcode, fixdlen, Ndist);
  }
  s->state = FlateOut;
  s->in = s->inend = 0;
  s->dst = s->dstbegin = s->dstbuf;
  s->pos = s->startpos = s->endpos = WinSize;
  s->eof = 0;
  s->skip = 0;
  s->prevm.len = 0;
  return s;
}


/* extern */

int deflate(FlateStream *stream)
{
  State *s = stream->state;
  int n, k;

  if (stream->err) {
    free(s);
    stream->state = 0;
    return FlateErr;
  }
  if (!s) {
    s = stream->state = alloc_state();
    if (!s) return stream->err = "no mem.", FlateErr;
  }

  if (stream->nin) {
    s->in = stream->in;
    s->inend = s->in + stream->nin;
    stream->nin = 0;
  }
  n = deflate_state(s);

  if (n == FlateOut) {
    k = s->dst - s->dstbegin;
    if (k < stream->nout) stream->nout = k;
    memcpy(stream->out, s->dstbegin, stream->nout);
    s->dstbegin += stream->nout;
  }

  if (n == FlateOk || n == FlateErr) {
    free(s);
    stream->state = 0;
  }

  return n;
}

static void set32(uchar *p, uint n)
{
  p[0] = n >> 24;
  p[1] = n >> 16;
  p[2] = n >> 8;
  p[3] = n;
}

static void set32le(uchar *p, uint n)
{
  p[0] = n;
  p[1] = n >> 8;
  p[2] = n >> 16;
  p[3] = n >> 24;
}

static int check32(uchar *p, uint n)
{
  return n == ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static int check32le(uchar *p, uint n)
{
  return n == (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

enum {
  ZlibCM    = 7 << 4,
  ZlibCINFO = 8,
  ZlibFLEV  = 3 << 6,
  ZlibFDICT = 1 << 5,
  ZlibFCHK  = 31 - (((ZlibCM | ZlibCINFO) << 8) | ZlibFLEV) % 31
};

int deflate_zlib_header(uchar *p, int n)
{
  if (n < 2) return FlateErr;
  p[0] = ZlibCM | ZlibCINFO;  /* deflate method, 32K window size */
  p[1] = ZlibFLEV | ZlibFCHK; /* highest compression */

  return 2;
}

int deflate_zlib_footer(uchar *p, int n, uint sum, uint len, uint zlen)
{
  if (n < 4) return FlateErr;
  set32(p, sum);

  return 4;
}

int inflate_zlib_header(uchar *p, int n)
{
  if (n < 2) return FlateErr;
  if (((p[0] << 8) | p[1]) % 31) return FlateErr;
  if ((p[0] & 0xf0) != ZlibCM || (p[0] & 0x0f) > ZlibCINFO) return FlateErr;
  if (p[1] & ZlibFDICT) return FlateErr;

  return 2;
}

int inflate_zlib_footer(uchar *p, int n, uint sum, uint len, uint zlen)
{
  if (n < 4 || !check32(p, sum))
    return FlateErr;
  return 4;
}


enum {
  GZipID1    = 0x1f,
  GZipID2    = 0x8b,
  GZipCM     = 8,
  GZipFHCRC  = 1 << 1,
  GZipFEXTRA = 1 << 2,
  GZipFNAME  = 1 << 3,
  GZipFCOMM  = 1 << 4,
  GZipXFL    = 2,
  GZipOS     = 255
};

int deflate_gzip_header(uchar *p, int n)
{
  if (n < 10)
    return FlateErr;
  memset(p, 0, 10);
  p[0] = GZipID1;
  p[1] = GZipID2;
  p[2] = GZipCM;
  p[8] = GZipXFL;
  p[9] = GZipOS;
  return 10;
}

int deflate_gzip_footer(uchar *p, int n, uint sum, uint len, uint zlen)
{
  if (n < 8)
    return FlateErr;
  set32le(p, sum);
  set32le(p+4, len);
  return 8;
}

int inflate_gzip_header(uchar *p, int n)
{
  int k = 10;

  if (k > n) return FlateErr;
  if (p[0] != GZipID1 || p[1] != GZipID2 || p[2] != GZipCM) return FlateErr;
  if (p[3] & GZipFEXTRA) {
    k += 2 + ((p[k] << 8) | p[k+1]);
    if (k > n) return FlateErr;
  }
  if (p[3] & GZipFNAME) {
    for (; k < n; k++) if (p[k] == 0) break;
    k++;
    if (k > n) return FlateErr;
  }
  if (p[3] & GZipFCOMM) {
    for (; k < n; k++) if (p[k] == 0) break;
    k++;
    if (k > n) return FlateErr;
  }
  if (p[3] & GZipFHCRC) {
    k += 2;
    if (k > n) return FlateErr;
  }

  return k;
}

int inflate_gzip_footer(uchar *p, int n, uint sum, uint len, uint zlen)
{
  if (n < 8 || !check32le(p, sum) || !check32le(p+4, len)) return FlateErr;

  return 8;
}


static char pkname[] = "sflate_stream";

enum {
  PKHeadID   = 0x04034b50,
  PKDataID   = 0x08074b50,
  PKDirID    = 0x02014b50,
  PKFootID   = 0x06054b50,
  PKVersion  = 20,
  PKFlag     = 1 << 3,
  PKMethod   = 8,
  PKDate     = ((2009 - 1980) << 25) | (1 << 21) | (1 << 16),
  PKHeadSize = 30,
  PKDirSize  = 46,
  PKNameLen  = sizeof(pkname) - 1
};

int deflate_pkzip_header(uchar *p, int n)
{
  if (n < PKHeadSize + PKNameLen) return FlateErr;
  memset(p, 0, PKHeadSize);
  set32le(p, PKHeadID);
  set32le(p+4, PKVersion);
  set32le(p+6, PKFlag);
  set32le(p+8, PKMethod);
  set32le(p+10, PKDate);
  set32le(p+26, PKNameLen);
  memcpy(p + PKHeadSize, pkname, PKNameLen);
  return PKHeadSize + PKNameLen;
}

int deflate_pkzip_footer(uchar *p, int n, uint sum, uint len, uint zlen)
{
  if (n < PKDirSize + PKNameLen + 22) return FlateErr;
  /* unzip bug */
/*
  if (n < 16 + PKDirSize + PKNameLen + 22) return FlateErr;
  set32le(p, PKDataID);
  set32le(p+4, sum);
  set32le(p+8, zlen);
  set32le(p+12, len);
  p += 16;
*/
  memset(p, 0, PKDirSize);
  set32le(p, PKDirID);
  set32le(p+4, PKVersion | (PKVersion << 16));
  set32le(p+8, PKFlag);
  set32le(p+10, PKMethod);
  set32le(p+12, PKDate);
  set32le(p+16, sum);
  set32le(p+20, zlen);
  set32le(p+24, len);
  set32le(p+28, PKNameLen);
  memcpy(p + PKDirSize, pkname, PKNameLen);
  p += PKDirSize + PKNameLen;
  memset(p, 0, 22);
  set32le(p, PKFootID);
  p[8] = p[10] = 1;
  set32le(p+12, PKDirSize + PKNameLen);
  set32le(p+16, zlen + PKHeadSize + PKNameLen);
  return PKDirSize + PKNameLen + 22;
/*
  set32le(p+12, 16 + PKDirSize + PKNameLen);
  set32le(p+16, zlen + PKHeadSize + PKNameLen);
  return 16 + PKDirSize + PKNameLen + 22;
*/
}

int inflate_pkzip_header(uchar *p, int n)
{
  int k = 30;

  if (k > n) return FlateErr;
  if (!check32le(p, PKHeadID)) return FlateErr;
  if ((p[4] | (p[5] << 8)) > PKVersion) return FlateErr;
  if ((p[8] | (p[9] << 8)) != PKMethod) return FlateErr;
  k += p[26] | (p[27] << 8);
  k += p[28] | (p[29] << 8);
  if (k > n) return FlateErr;

  return k;
}

int inflate_pkzip_footer(uchar *p, int n, uint sum, uint len, uint zlen)
{
  int k = PKDirSize + 22;

  if (k > n) return FlateErr;
  if (check32le(p, PKDataID)) {
    p += 16;
    k += 16;
    if (k > n) return FlateErr;
  }
  if (!check32le(p, PKDirID)) return FlateErr;
  if (!check32le(p+16, sum)) return FlateErr;
  if (!check32le(p+20, zlen)) return FlateErr;
  if (!check32le(p+24, len)) return FlateErr;

  return k;
}


/* example usage */

static int (*header)(uchar *, int);
static int (*footer)(uchar *, int, uint, uint, uint);
static uint (*checksum)(uchar *, int, uint);
static char *err;
static uint sum;
static uint nin;
static uint nout;
static uint headerlen;
static uint footerlen;
static uint extralen;

static int dummyheader(uchar *p, int n) {
  return 0;
}
static int dummyfooter(uchar *p, int n, uint sum, uint len, uint zlen) {
  return 0;
}
static uint dummysum(uchar *p, int n, uint sum) {
  return 0;
}

/* compress, using FlateStream interface */
int compress_stream(FILE *in, FILE *out)
{
  FlateStream s;
  int k, n;
  enum {Nin = 1<<15, Nout = 1<<15};

  s.in = malloc(Nin);
  s.out = malloc(Nout);
  s.nin = 0;
  s.nout = Nout;
  s.err = 0;
  s.state = 0;

  k = header(s.out, s.nout);
  if (k == FlateErr) {
    s.err = "header error.";
    n = FlateErr;
  } else {
    headerlen = s.nout = k;
    n = FlateOut;
  }
  for (;; n = deflate(&s))
    switch (n) {
    case FlateOk:
      k = footer(s.out, s.nout, sum, nin, nout - headerlen);
      if (k == FlateErr) {
        s.err = "footer error.";
        n = FlateErr;
      } else if (k != fwrite(s.out, 1, k, out)) {
        s.err = "write error.";
        n = FlateErr;
      } else {
        footerlen = k;
        nout += k;
      }
    case FlateErr:
      free(s.in);
      free(s.out);
      err = s.err;
      return n;
    case FlateIn:
      s.nin = fread(s.in, 1, Nin, in);
      nin += s.nin;
      sum = checksum(s.in, s.nin, sum);
      break;
    case FlateOut:
      k = fwrite(s.out, 1, s.nout, out);
      if (k != s.nout)
        s.err = "write error.";
      nout += k;
      s.nout = Nout;
      break;
    }
}

/* decompress, using FlateStream interface */
int decompress_stream(FILE *in, FILE *out)
{
  FlateStream s;
  uchar *begin;
  int k, n;
  enum {Nin = 1<<15, Nout = 1<<15};

  s.in = begin = malloc(Nin);
  s.out = malloc(Nout);
  s.nout = Nout;
  s.err = 0;
  s.state = 0;

  s.nin = fread(s.in, 1, Nin, in);
  nin += s.nin;
  k = header(s.in, s.nin);
  if (k == FlateErr) {
    s.err = "header error.";
    n = FlateErr;
  } else {
    headerlen = k;
    s.nin -= k;
    s.in += k;
    n = inflate(&s);
  }
  for (;; n = inflate(&s))
    switch (n) {
    case FlateOk:
      memmove(begin, s.in, s.nin);
      k = fread(begin, 1, Nin-s.nin, in);
      nin += k;
      s.nin += k;
      k = footer(begin, s.nin, sum, nout, nin - s.nin - headerlen);
      if (k == FlateErr) {
        s.err = "footer error.";
        n = FlateErr;
      } else {
        footerlen = k;
        extralen = s.nin - k;
      }
    case FlateErr:
      free(begin);
      free(s.out);
      err = s.err;
      return n;
    case FlateIn:
      s.in = begin;
      s.nin = fread(s.in, 1, Nin, in);
      nin += s.nin;
      break;
    case FlateOut:
      k = fwrite(s.out, 1, s.nout, out);
      if (k != s.nout)
        s.err = "write error.";
      sum = checksum(s.out, k, sum);
      nout += k;
      s.nout = Nout;
      break;
    }
}

static int old_main(int argc, char *argv[])
{
  char comp = 'c';
  char fmt = 'r';
  char verbose = 'q';
  int (*call)(FILE *, FILE*);
  int n, i;

  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-' && argv[i][1] && argv[i][2] == 0)
      switch (argv[i][1]) {
      case 'q':
      case 'v':
        verbose = argv[i][1];
        continue;
      case 'c':
      case 'd':
        comp = argv[i][1];
        continue;
      case 'r':
      case 'g':
      case 'z':
      case 'p':
        fmt = argv[i][1];
        continue;
      }
    fprintf(stderr, "usage: %s [-q|-v] [-c|-d] [-r|-g|-z|-p]\n\n"
      "deflate stream compression\n"
      " -q quiet (default)\n"
      " -v verbose\n"
      " -c compress (default)\n"
      " -d decompress\n"
      " -r raw (default)\n"
      " -g gzip\n"
      " -z zlib\n"
      " -p pkzip\n", argv[0]);
    return -1;
  }
  call = comp == 'c' ? compress_stream : decompress_stream;
  switch (fmt) {
  case 'r':
    header = dummyheader;
    footer = dummyfooter;
    checksum = dummysum;
    n = call(stdin, stdout);
    break;
  case 'g':
    if (comp == 'c') {
      header = deflate_gzip_header;
      footer = deflate_gzip_footer;
    } else {
      header = inflate_gzip_header;
      footer = inflate_gzip_footer;
    }
    checksum = crc32;
    crc32init();
    n = call(stdin, stdout);
    break;
  case 'z':
    if (comp == 'c') {
      header = deflate_zlib_header;
      footer = deflate_zlib_footer;
    } else {
      header = inflate_zlib_header;
      footer = inflate_zlib_footer;
    }
    checksum = adler32;
    n = call(stdin, stdout);
    break;
  case 'p':
    if (comp == 'c') {
      header = deflate_pkzip_header;
      footer = deflate_pkzip_footer;
    } else {
      header = inflate_pkzip_header;
      footer = inflate_pkzip_footer;
    }
    checksum = crc32;
    crc32init();
    n = call(stdin, stdout);
    break;
  default:
    err = "uninplemented.";
    n = FlateErr;
    break;
  }
  if (verbose == 'v')
    fprintf(stderr, "in:%d out:%d checksum: 0x%08x (header:%d data:%d footer:%d extra input:%s)\n",
      nin, nout, sum, headerlen, (comp == 'c' ? nout : nin) - headerlen - footerlen - extralen,
      footerlen, extralen ? "yes" : "no");
  if (n != FlateOk)
    fprintf(stderr, "error: %s\n", err);
  return n;
}

// Total hack
void gzip_main(void)
{
  int i;

  for (i=0; toys.argv[i]; i++);
  old_main(i, toys.argv);
}
