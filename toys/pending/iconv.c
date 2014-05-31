/* iconv.c - Convert character encoding
 *
 * Copyright 2014 Felix Janda <felix.janda@posteo.de>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/iconv.html

USE_ICONV(NEWTOY(iconv, "cst:f:", TOYFLAG_USR|TOYFLAG_BIN))

config ICONV
  bool "iconv"
  default n
  depends on TOYBOX_ICONV
  help
    usage: iconv [-f FROM] [-t TO] [FILE...]

    Convert character encoding of files.

    -f  convert from (default utf8)
    -t  convert to   (default utf8)
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
  size_t inleft = 0;
  int len = 1;

  do {
    size_t outleft = 2048;
    char *in = toybuf+inleft, *out = outstart;

    len = read(fd, in, 2048-inleft);

    if (len < 0) {
      perror_msg("read '%s'");
      return;
    }
    inleft += len;

    do {
      if (iconv(TT.ic, &in, &inleft, &out, &outleft) == -1
          && (errno == EILSEQ || (in == toybuf+inleft-len && errno == EINVAL)))
      {
        if (outleft) {
          // Skip first byte of illegal sequence to avoid endless loops
          *(out++) = *(in++);
          inleft--;
        }
      }
      xwrite(1, outstart, out-outstart);
      // Top off input buffer
      memmove(in, toybuf, inleft);
    } while (len < 1 && inleft);
  } while (len > 0);
}

void iconv_main(void)
{
  TT.ic = iconv_open(TT.to ? TT.to : "utf8", TT.from ? TT.from : "utf8");
  if (TT.ic == (iconv_t)-1) error_exit("bad encoding");
  loopfiles(toys.optargs, do_iconv);
  if (CFG_TOYBOX_FREE) iconv_close(TT.ic);
}
