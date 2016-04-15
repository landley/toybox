/* rev.c - reverse lines of a set of given input files
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_REV(NEWTOY(rev, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config REV
  bool "rev"
  default y
  help
    usage: rev [FILE...]

    Output each line reversed, when no files are given stdin is used.
*/

#include "toys.h"

static void do_rev(int fd, char *name)
{
  char *c;

  for (;;) {
    unsigned len, i;

    if (!(c = get_line(fd))) break;
    len = strlen(c);
    if (len--) for (i = 0; i <= len/2; i++) {
      char tmp = c[i];

      c[i] = c[len-i];
      c[len-i] = tmp;
    }
    xputs(c);
    free(c);
  }
}

void rev_main(void)
{
  loopfiles(toys.optargs, do_rev);
}
