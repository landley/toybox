/* basename.c - Return non-directory portion of a pathname
 *
 * Copyright 2012 Tryn Mirell <tryn@mirell.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/basename.html


USE_BASENAME(NEWTOY(basename, "<1>2", TOYFLAG_USR|TOYFLAG_BIN))

config BASENAME
  bool "basename"
  default y
  help
    usage: basename string [suffix]

    Return non-directory portion of a pathname removing suffix
*/

#include "toys.h"

void basename_main(void)
{
  char *base = basename(*toys.optargs), *suffix = toys.optargs[1];

  // chop off the suffix if provided
  if (suffix) {
    char *s = base + strlen(base) - strlen(suffix);
    if (s > base && !strcmp(s, suffix)) *s = 0;
  }

  puts(base);
}
