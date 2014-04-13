/* iconv.c - Convert character encoding
 *
 * Copyright 2014 Felix Janda <felix.janda@posteo.de>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/

USE_ICONV(NEWTOY(iconv, "t:f:", TOYFLAG_USR|TOYFLAG_BIN))

config ICONV
  bool "iconv"
  default n
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
)

iconv_t ic; // Can't be put into GLOBALS because iconv_t is defined in iconv.h

static void do_iconv(int fd, char *name)
{
  size_t inleft = 0, outleft = 2048;
  char *in = toybuf, *out = toybuf + 2048;

  while (1) {
    if (!inleft || (errno == EINVAL)) {
      memmove(in, toybuf, inleft);
      if (!(inleft += read(fd, toybuf + inleft, 2048 - inleft))) {
        xwrite(1, toybuf + 2048, 2048 - outleft);
        break;
      }
      in = toybuf;
    } else if (errno == E2BIG) {
      xwrite(1, toybuf + 2048, 2048 - outleft);
      out = toybuf + 2048;
      outleft = 2048;
    } else if (errno == EILSEQ) {
      in++;
      inleft--;
    }
    if (iconv(ic, &in, &inleft, &out, &outleft) != (size_t)-1) errno = 0;
  }
}

void iconv_main(void)
{
  char *from = "utf8", *to = "utf8";

  if (toys.optflags & FLAG_f) from = TT.from;
  if (toys.optflags & FLAG_t) to = TT.to;
  if ((ic = iconv_open(to, from)) == (iconv_t)-1) error_exit("iconv_open");
  loopfiles(toys.optargs, do_iconv);
  if (CFG_TOYBOX_FREE) iconv_close(ic);
}
