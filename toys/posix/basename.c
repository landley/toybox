/* basename.c - Return non-directory portion of a pathname
 *
 * Copyright 2012 Tryn Mirell <tryn@mirell.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/basename.html


USE_BASENAME(NEWTOY(basename, "^<1as:", TOYFLAG_USR|TOYFLAG_BIN))

config BASENAME
  bool "basename"
  default y
  help
    usage: basename [-a] [-s SUFFIX] NAME... | NAME [SUFFIX]

    Return non-directory portion of a pathname removing suffix.

    -a		All arguments are names
    -s SUFFIX	Remove suffix (implies -a)
*/

#define FOR_basename
#include "toys.h"

GLOBALS(
  char *s;
)

void basename_main(void)
{
  char **arg;

  if (toys.optflags&FLAG_s) toys.optflags |= FLAG_a;

  if (!(toys.optflags&FLAG_a)) {
    if (toys.optc > 2) error_exit("too many args");
    TT.s = toys.optargs[1];
    toys.optargs[1] = NULL;
  }

  for (arg = toys.optargs; *arg; ++arg) {
    char *base = basename(*arg), *p;

    // Chop off the suffix if provided.
    if (TT.s && *TT.s && (p = strend(base, TT.s))) *p = 0;
    puts(base);
  }
}
