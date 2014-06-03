/* cat.c - copy inputs to stdout.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/cat.html

USE_CAT(NEWTOY(cat, "u", TOYFLAG_BIN))

config CAT
  bool "cat"
  default y
  help
    usage: cat [-u] [file...]

    Copy (concatenate) files to stdout.  If no files listed, copy from stdin.
    Filename "-" is a synonym for stdin.

    -u	Copy one byte at a time (slow).
*/

#include "toys.h"

static void do_cat(int fd, char *name)
{
  int len, size=toys.optflags ? 1 : sizeof(toybuf);

  for (;;) {
    len = read(fd, toybuf, size);
    if (len<0) perror_msg("%s",name);
    if (len<1) break;
    xwrite(1, toybuf, len);
  }
}

void cat_main(void)
{
  loopfiles(toys.optargs, do_cat);
}
