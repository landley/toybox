/* readlink.c - Return string representation of a symbolic link.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>

USE_READLINK(NEWTOY(readlink, "<1>1nqmef[-mef]", TOYFLAG_USR|TOYFLAG_BIN))

config READLINK
  bool "readlink"
  default y
  help
    usage: readlink FILE

    With no options, show what symlink points to, return error if not symlink.

    Options for producing cannonical paths (all symlinks/./.. resolved):

    -e	Canonical path to existing entry (fail if missing)
    -f	Full path (fail if directory missing)
    -m	Ignore missing entries, show where it would be
    -n	No trailing newline
    -q	Quiet (no output, just error code)
*/

#define FOR_readlink
#include "toys.h"

void readlink_main(void)
{
  char *s;

  // Calculating full cannonical path?
  // Take advantage of flag positions to calculate m = -1, f = 0, e = 1
  if (toys.optflags & (FLAG_f|FLAG_e|FLAG_m))
    s = xabspath(*toys.optargs, (toys.optflags&(FLAG_f|FLAG_e))-1);
  else s = xreadlink(*toys.optargs);

  if (s) {
    if (!(toys.optflags & FLAG_q))
      xprintf((toys.optflags & FLAG_n) ? "%s" : "%s\n", s);
    if (CFG_TOYBOX_FREE) free(s);
  } else toys.exitval = 1;
}
