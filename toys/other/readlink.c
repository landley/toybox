/* readlink.c - Return string representation of a symbolic link.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>

USE_READLINK(NEWTOY(readlink, "<1>1fenq[-fe]", TOYFLAG_USR|TOYFLAG_BIN))

config READLINK
  bool "readlink"
  default y
  help
    usage: readlink FILE

    With no options, show what symlink points to, return error if not symlink.

    Options for producing cannonical paths (all symlinks/./.. resolved):

    -e	cannonical path to existing entry (fail if missing)
    -f	full path (fail if directory missing)
    -n	no trailing newline
    -q	quiet (no output, just error code)
*/

#define FOR_readlink
#include "toys.h"

void readlink_main(void)
{
  char *s;

  // Calculating full cannonical path?

  if (toys.optflags & (FLAG_f|FLAG_e))
    s = xabspath(*toys.optargs, toys.optflags & FLAG_e);
  else s = xreadlink(*toys.optargs);

  if (s) {
    if (!(toys.optflags & FLAG_q))
      xprintf((toys.optflags & FLAG_n) ? "%s" : "%s\n", s);
    if (CFG_TOYBOX_FREE) free(s);
  } else toys.exitval = 1;
}
