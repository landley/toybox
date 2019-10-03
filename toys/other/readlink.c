/* readlink.c - Return string representation of a symbolic link.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>

USE_READLINK(NEWTOY(readlink, "<1nqmef(canonicalize)[-mef]", TOYFLAG_USR|TOYFLAG_BIN))

config READLINK
  bool "readlink"
  default y
  help
    usage: readlink FILE...

    With no options, show what symlink points to, return error if not symlink.

    Options for producing canonical paths (all symlinks/./.. resolved):

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
  char **arg, *s;

  for (arg = toys.optargs; *arg; arg++) {
    // Calculating full canonical path?
    // Take advantage of flag positions to calculate m = -1, f = 0, e = 1
    if (toys.optflags & (FLAG_f|FLAG_e|FLAG_m))
      s = xabspath(*arg, (toys.optflags&(FLAG_f|FLAG_e))-1);
    else s = xreadlink(*arg);

    if (s) {
      if (!FLAG(q)) xprintf(FLAG(n) ? "%s" : "%s\n", s);
      if (CFG_TOYBOX_FREE) free(s);
    } else toys.exitval = 1;
  }
}
