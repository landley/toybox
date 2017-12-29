/* iconv.c - Convert character encoding
 *
 * Copyright 2014 Felix Janda <felix.janda@posteo.de>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/iconv.html
 *
 * Deviations from posix: no idea how to implement -l

USE_ICONV(NEWTOY(iconv, "cst:f:", TOYFLAG_USR|TOYFLAG_BIN))

config ICONV
  bool "iconv"
  default y
  depends on TOYBOX_ICONV
  help
    usage: iconv [-f FROM] [-t TO] [FILE...]

    Convert character encoding of files.

    -c	Omit invalid chars
    -f	convert from (default utf8)
    -t	convert to   (default utf8)
*/

#define FOR_iconv
#include "toys.h"
#include <iconv.h>

GLOBALS(
  char *from;
  char *to;

  void *ic;
)

static void do_iconv(int fd, char *name)
{
  char *outstart = toybuf+2048;
  size_t outlen, inlen = 0;
  int readlen = 1;

  for (;;) {
    char *in = toybuf, *out = outstart;

    if (readlen && 0>(readlen = read(fd, in+inlen, 2048-inlen))) {
      perror_msg("read '%s'", name);
      return;
    }
    inlen += readlen;
    if (!inlen) break;

    outlen = 2048;
    iconv(TT.ic, &in, &inlen, &out, &outlen);
    if (in == toybuf) {
      // Skip first byte of illegal sequence to avoid endless loops
      if (toys.optflags & FLAG_c) in++;
      else *(out++) = *(in++);
      inlen--;
    }
    if (out != outstart) xwrite(1, outstart, out-outstart);
    memmove(toybuf, in, inlen);
  }
}

void iconv_main(void)
{
  if (!TT.to) TT.to = "utf8";
  if (!TT.from) TT.from = "utf8";

  if ((iconv_t)-1 == (TT.ic = iconv_open(TT.to, TT.from)))
    perror_exit("%s/%s", TT.to, TT.from);
  loopfiles(toys.optargs, do_iconv);
  if (CFG_TOYBOX_FREE) iconv_close(TT.ic);
}
