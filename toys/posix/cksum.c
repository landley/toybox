/* cksum.c - produce crc32 checksum value for each input
 *
 * Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/cksum.html

USE_CKSUM(NEWTOY(cksum, "HIPLN", TOYFLAG_BIN))
USE_CRC32(NEWTOY(crc32, 0, TOYFLAG_BIN))

config CKSUM
  bool "cksum"
  default y
  help
    usage: cksum [-IPLN] [file...]

    For each file, output crc32 checksum value, length and name of file.
    If no files listed, copy from stdin.  Filename "-" is a synonym for stdin.

    -H	Hexadecimal checksum (defaults to decimal)
    -L	Little endian (defaults to big endian)
    -P	Pre-inversion
    -I	Skip post-inversion
    -N	Do not include length in CRC calculation (or output)

config CRC32
  bool "crc32"
  default y
  help
    usage: crc32 [file...]

    Output crc32 checksum for each file.
*/

#define FOR_cksum
#define FORCE_FLAGS
#include "toys.h"

GLOBALS(
  unsigned crc_table[256];
)

static unsigned cksum_be(unsigned crc, unsigned char c)
{
  return (crc<<8)^TT.crc_table[(crc>>24)^c];
}

static unsigned cksum_le(unsigned crc, unsigned char c)
{
  return TT.crc_table[(crc^c)&0xff] ^ (crc>>8);
}

static void do_cksum(int fd, char *name)
{
  unsigned crc = (toys.optflags & FLAG_P) ? 0xffffffff : 0;
  uint64_t llen = 0, llen2;
  unsigned (*cksum)(unsigned crc, unsigned char c);
  int len, i;

  cksum = (toys.optflags & FLAG_L) ? cksum_le : cksum_be;
  // CRC the data

  for (;;) {
    len = read(fd, toybuf, sizeof(toybuf));
    if (len<0) perror_msg_raw(name);
    if (len<1) break;

    llen += len;
    for (i=0; i<len; i++) crc=cksum(crc, toybuf[i]);
  }

  // CRC the length

  llen2 = llen;
  if (!(toys.optflags & FLAG_N)) {
    while (llen) {
      crc = cksum(crc, llen);
      llen >>= 8;
    }
  }

  printf((toys.optflags & FLAG_H) ? "%08x" : "%u",
    (toys.optflags & FLAG_I) ? crc : ~crc);
  if (!(toys.optflags&FLAG_N)) printf(" %"PRIu64, llen2);
  if (toys.optc) printf(" %s", name);
  xputc('\n');
}

void cksum_main(void)
{
  crc_init(TT.crc_table, toys.optflags & FLAG_L);
  loopfiles(toys.optargs, do_cksum);
}

void crc32_main(void)
{
  toys.optflags |= FLAG_H|FLAG_N|FLAG_P|FLAG_L;
  if (toys.optc) toys.optc--;
  cksum_main();
}
