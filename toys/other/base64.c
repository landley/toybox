/* base64.c - Encode and decode base64
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * No standard

USE_BASE64(NEWTOY(base64, "diw#<0=76[!dw]", TOYFLAG_USR|TOYFLAG_BIN))

config BASE64
  bool "base64"
  default y
  help
    usage: base64 [-di] [-w COLUMNS] [FILE...]

    Encode or decode in base64.

    -d	decode
    -i	ignore non-alphabetic characters
    -w	wrap output at COLUMNS (default 76 or 0 for no wrap)
*/

#define FOR_base64
#include "toys.h"

GLOBALS(
  long columns;

  unsigned total;
)

static void wraputchar(int c, int *x)
{
  putchar(c);
  TT.total++;
  if (TT.columns && ++*x == TT.columns) {
    *x = 0;
    xputc('\n');
  };
}

static void do_base64(int fd, char *name)
{
  int out = 0, bits = 0, x = 0, i, len;
  char *buf = toybuf+128;

  TT.total = 0;

  for (;;) {
    // If no more data, flush buffer
    if (!(len = xread(fd, buf, sizeof(toybuf)-128))) {
      if (!(toys.optflags & FLAG_d)) {
        if (bits) wraputchar(toybuf[out<<(6-bits)], &x);
        while (TT.total&3) wraputchar('=', &x);
        if (x) xputc('\n');
      }

      return;
    }

    for (i=0; i<len; i++) {
      if (toys.optflags & FLAG_d) {
        if (buf[i] == '=') return;

        if ((x = stridx(toybuf, buf[i])) != -1) {
          out = (out<<6) + x;
          bits += 6;
          if (bits >= 8) {
            putchar(out >> (bits -= 8));
            out &= (1<<bits)-1;
            if (ferror(stdout)) perror_exit(0);
          }

          continue;
        }
        if (buf[i] == '\n' || (toys.optflags & FLAG_i)) continue;

        break;
      } else {
        out = (out<<8) + buf[i];
        bits += 8;
        while (bits >= 6) {
          wraputchar(toybuf[out >> (bits -= 6)], &x);
          out &= (1<<bits)-1;
        }
      }
    }
  }
}

void base64_main(void)
{
  base64_init(toybuf);
  loopfiles(toys.optargs, do_base64);
}
