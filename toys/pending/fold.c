/* fold.c - fold text
 *
 * Copyright 2014 Samuel Holland <samuel@sholland.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/fold.html

USE_FOLD(NEWTOY(fold, "bsw#", TOYFLAG_USR|TOYFLAG_BIN))

config FOLD
  bool "fold"
  default n
  help
    usage: fold [-bs] [-w WIDTH] [FILE...]

    Folds/wraps FILE or stdin at 80 columns.

    -b  Wrap based on bytes instead of columns
    -s  Wrap at farthest right whitespace
    -w  Wrap at WIDTH columns instead of 80
*/

#define FOR_fold
#include "toys.h"

GLOBALS(
  int w_number;
)

void do_fold(int fd, char *name)
{
  int buflen, i, len = 0, split;
  int max = (toys.optflags & FLAG_w) ? TT.w_number : 80;
  char tmp, *buf;

  if (max > sizeof(toybuf)) {
    error_exit("width (%ld) too big", max);
  }

  while (read(fd, toybuf, sizeof(toybuf))) {
    split = -1;
    buf = toybuf;
    buflen = strlen(buf);

    for (i = 0; i < buflen; i++) {
      switch (buf[i]) {
        case '\n':
          //reset len, FLAG_b or not; just print multiple lines at once
          len = 0;
          continue;
        case '\b':
          //len cannot be negative; not allowed to wrap after backspace
          if (toys.optflags & FLAG_b) len++;
          else if (len > 0) len--;
          continue;
        case '\r':
          //not allowed to wrap after carriage return
          if (toys.optflags & FLAG_b) len++;
          else len = 0;
          continue;
        case '\t':
          //round to 8, but we add one after falling through
          //(because of whitespace, but it also takes care of FLAG_b)
          if (!(toys.optflags & FLAG_b)) len = (len & -8) + 7;
        case ' ':
          split = i;
        default:
          len++;
      }

      //we don't want to double up \n; not allowed to wrap before \b
      if (len >= max && buf[i+1] != '\n' && buf[i+1] != '\b') {
        if (!(toys.optflags & FLAG_s)) split = i; //we split right here
        tmp = buf[split+1];
        buf[split+1] = 0;
        xprintf("%s\n", buf);
        buf[split+1] = tmp;
        len = 0;
        if (split < buflen - 1) {
          buf += split + 1;
          i = 0;
          buflen = strlen(buf);
        }
      }
    }

    xputs(buf);
  }
}

void fold_main(void)
{
  loopfiles(toys.optargs, do_fold);
}
