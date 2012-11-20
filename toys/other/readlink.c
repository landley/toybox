/* readlink.c - Return string representation of a symbolic link.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>

USE_READLINK(NEWTOY(readlink, "<1>1femnq[-fem]", TOYFLAG_BIN))

config READLINK
  bool "readlink"
  default n
  help
    usage: readlink FILE

    With no options, show what symlink points to, return error if not symlink.

    Options for producing cannonical paths (all symlinks/./.. resolved):

    -e	cannonical path to existing file (fail if does not exist)
    -f	cannonical path to creatable file (fail if directory does not exist)
    -m	cannonical path
    -n	no trailing newline
    -q	quiet (no output, just error code)
*/

#define FOR_readlink
#include "toys.h"

void readlink_main(void)
{
  char *s;

  // Calculating full cannonical path?

  if (toys.optflags & (FLAG_f|FLAG_e|FLAG_m)) {
    unsigned u = 0;

    if (toys.optflags & FLAG_f) u++;
    if (toys.optflags & FLAG_m) u=999999999;

    s = xabspath(*toys.optargs, u);
  } else s = xreadlink(*toys.optargs);

  if (s) {
    if (!(toys.optflags & FLAG_q))
      xprintf((toys.optflags & FLAG_n) ? "%s" : "%s\n", s);
    if (CFG_TOYBOX_FREE) free(s);
  } else toys.exitval = 1;
}
