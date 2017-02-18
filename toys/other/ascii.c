/* ascii.c - display ascii table
 *
 * Copyright 2017 Rob Landley <rob@landley.net>
 *
 * Technically 7-bit ASCII is ANSI X3.4-1986, a standard available as
 * INCITS 4-1986[R2012] on ansi.org, but they charge for it.

USE_ASCII(NEWTOY(ascii, 0, TOYFLAG_USR|TOYFLAG_BIN))

config ASCII
  bool "ascii"
  default n
  help
    usage: ascii

    Display ascii character set.
*/

#include "toys.h"

void ascii_main(void)
{
  char *low="NULSOHSTXETXEOTENQACKBELBS HT LF VT FF CR SO SI DLEDC1DC2DC3DC4"
            "NAKSYNETBCANEM SUBESCFS GS RS US ";
  int x, y;

  for (x = 0; x<8; x++) printf("Dec Hex%*c", 2+2*(x<2)+(x>4), ' ');
  xputc('\n');
  for (y=0; y<=15; y++) {
    for (x=0; x<8; x++) {
      int i = x*16+y;

      if (i>95 && i<100) putchar(' ');
      printf("% 3d %02X ", i, i);
      if (i<32 || i==127) printf("%.3s ", (i==127) ? "DEL" : low+3*i);
      else printf("%c ", i);
    }
    xputc('\n');
  }
}
