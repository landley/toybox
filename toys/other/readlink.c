/* readlink.c - Return string representation of a symbolic link.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>

// -ef positions match ABS_FILE ABS_PATH
USE_READLINK(NEWTOY(readlink, "<1nqmef(canonicalize)[-mef]", TOYFLAG_USR|TOYFLAG_BIN))
USE_REALPATH(OLDTOY(realpath, readlink, TOYFLAG_USR|TOYFLAG_BIN))

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

config REALPATH
  bool "realpath"
  default y
  help
    usage: realpath FILE...

    Display the canonical absolute pathname
*/

#define FOR_readlink
#define FORCE_FLAGS
#include "toys.h"

void readlink_main(void)
{
  char **arg, *s;

  if (toys.which->name[3]=='l') toys.optflags |= FLAG_f;
  for (arg = toys.optargs; *arg; arg++) {
    // Calculating full canonical path?
    // Take advantage of flag positions: m = 0, f = ABS_PATH, e = ABS_FILE
    if (toys.optflags & (FLAG_f|FLAG_e|FLAG_m))
      s = xabspath(*arg, toys.optflags&(FLAG_f|FLAG_e));
    else s = xreadlink(*arg);

    if (s) {
      if (!FLAG(q)) xprintf("%s%s", s, (FLAG(n) && !arg[1]) ? "" : "\n");
      free(s);
    } else toys.exitval = 1;
  }
}
