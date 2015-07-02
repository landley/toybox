/* xxd.c - hexdump.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * No obvious standard, output looks like:
 * 0000000: 4c69 6e75 7820 7665 7273 696f 6e20 332e  Linux version 3.
 *
 * TODO: support for reversing a hexdump back into the original data.
 * TODO: -s seek

USE_XXD(NEWTOY(xxd, ">1c#<1>4096=16l#g#<1=2", TOYFLAG_USR|TOYFLAG_BIN))

config XXD
  bool "xxd"
  default n
  help
    usage: xxd [-c n] [-g n] [-l n] [file]

    Hexdump a file to stdout.  If no file is listed, copy from stdin.
    Filename "-" is a synonym for stdin.

    -c n	Show n bytes per line (default 16).
    -g n	Group bytes by adding a ' ' every n bytes (default 2).
    -l n	Limit of n bytes before stopping (default is no limit).
*/

#define FOR_xxd
#include "toys.h"

GLOBALS(
  long g;
  long l;
  long c;
)

static void do_xxd(int fd, char *name)
{
  long long pos = 0;
  int i, len, space;

  while (0<(len = readall(fd, toybuf, (TT.l && TT.l-pos<TT.c)?TT.l-pos:TT.c))) {
    printf("%08llx: ", pos);
    pos += len;
    space = 2*TT.c+TT.c/TT.g+1;

    for (i=0; i<len;) {
      space -= printf("%02x", toybuf[i]);
      if (!(++i%TT.g)) {
        putchar(' ');
        space--;
      }
    }

    printf("%*s", space, "");
    for (i=0; i<len; i++)
      putchar((toybuf[i]>=' ' && toybuf[i]<='~') ? toybuf[i] : '.');
    putchar('\n');
  }
  if (len<0) perror_exit("read");
}

void xxd_main(void)
{
  loopfiles(toys.optargs, do_xxd);
}
