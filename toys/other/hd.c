/* hd.c - Display traditional 16 byte/line hex+ascii.
 *
 * Copyright 2025 Rob Landley <rob@landley.net>
 *
 * No standard

USE_HD(NEWTOY(hd, 0, TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_LINEBUF))

config HD
  bool "hd"
  default y
  help
    usage: hd [FILE...]

    Display traditional 16 byte/line ascii+hex.
*/

#define FOR_hd
#include "toys.h"

GLOBALS(
    long len;
    int pos;
    char flush[16];
)

static void flush(void)
{
  int i;

  if (!TT.pos) return;
  printf("%*c", 3*(17-TT.pos)+(TT.pos<8), '|');
  for (i = 0; i<TT.pos; i++)
    putchar((TT.flush[i]<32 || TT.flush[i]>127) ? '.' : TT.flush[i]);
  printf("|\n");
  TT.pos = 0;
}

static void do_hd(int fd, char *name)
{
  int off, len;

  while (0<(len = xread(fd, toybuf, sizeof(toybuf)))) {
    for (off = 0; off<len; off++) {
      if (!TT.pos) printf("%08lx", TT.len);
      TT.len++;
      printf("  %02x"+!!(TT.pos&7), toybuf[off]);
      TT.flush[TT.pos++] = toybuf[off];
      if (TT.pos==16) flush();
    }
  }
}

void hd_main(void)
{
  int pos;

  loopfiles(toys.optargs, do_hd);
  pos = TT.pos;
  flush();
  if (pos) printf("%08lx\n", TT.len);
}
