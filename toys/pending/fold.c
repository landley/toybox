/* fold.c - fold text
 *
 * Copyright 2014 Samuel Holland <samuel@sholland.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/fold.html

USE_FOLD(NEWTOY(fold, "bsuw#<1", TOYFLAG_USR|TOYFLAG_BIN))

config FOLD
  bool "fold"
  default n
  help
    usage: fold [-bsu] [-w WIDTH] [FILE...]

    Folds (wraps) or unfolds ascii text by adding or removing newlines.
    Default line width is 80 columns for folding and infinite for unfolding.

    -b	Fold based on bytes instead of columns
    -s	Fold/unfold at whitespace boundaries if possible
    -u	Unfold text (and refold if -w is given)
    -w	Set lines to WIDTH columns or bytes
*/

#define FOR_fold
#include "toys.h"

GLOBALS(
  int width;
)

// wcwidth mbrtowc
void do_fold(int fd, char *name)
{
  int bufsz, len = 0, maxlen;

  if (toys.optflags & FLAG_w) maxlen = TT.width;
  else if (toys.optflags & FLAG_u) maxlen = 0;
  else maxlen = 80;

  while ((bufsz = read(fd, toybuf, sizeof(toybuf))) > 0) {
    char *buf = toybuf;
    int pos = 0, split = -1;

    while (pos < bufsz) {
      switch (buf[pos]) {
        case '\n':
          // print everything but the \n, then move on to the next buffer
          if ((toys.optflags & FLAG_u) && buf[pos-1] != '\n'
                                       && buf[pos+1] != '\n') {
              xwrite(1, buf, pos);
              bufsz -= pos + 1;
              buf += pos + 1;
              pos = 0;
              split = -1;
          // reset len, FLAG_b or not; just print multiple lines at once
          } else len = 0;
          break;
        case '\b':
          // len cannot be negative; not allowed to wrap after backspace
          if (toys.optflags & FLAG_b) len++;
          else if (len > 0) len--;
          break;
        case '\r':
          // not allowed to wrap after carriage return
          if (toys.optflags & FLAG_b) len++;
          else len = 0;
          break;
        case '\t':
          // round to 8, but we add one after falling through
          // (because of whitespace, but it also takes care of FLAG_b)
          if (!(toys.optflags & FLAG_b)) len = (len & ~7) + 7;
        case ' ':
          split = pos;
        default:
          len++;
      }

      // we don't want to double up \n; not allowed to wrap before \b
      if (maxlen > 0 && len >= maxlen && buf[pos+1] != '\n' && buf[pos+1] != '\b') {
        if (!(toys.optflags & FLAG_s) || split < 0) split = pos;
        xwrite(1, buf, split + 1);
        xputc('\n');
        bufsz -= split + 1;
        buf += split + 1;
        len = pos = 0;
        split = -1;
      } else pos++;
    }
    xwrite(1, buf, bufsz);
  }
  xputc('\n');
}

void fold_main(void)
{
  loopfiles(toys.optargs, do_fold);
}
