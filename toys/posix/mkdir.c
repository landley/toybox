/* mkdir.c - Make directories
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/mkdir.html

USE_MKDIR(NEWTOY(mkdir, "<1"USE_MKDIR_Z("Z:")"vp(parent)(parents)m:", TOYFLAG_BIN|TOYFLAG_UMASK))

config MKDIR
  bool "mkdir"
  default y
  help
    usage: mkdir [-vp] [-m mode] [dirname...]

    Create one or more directories.

    -m	set permissions of directory to mode
    -p	make parent directories as needed
    -v	verbose

config MKDIR_Z
  bool
  default y
  depends on MKDIR && !TOYBOX_LSM_NONE
  help
    usage: [-Z context]

    -Z	set security context
*/

#define FOR_mkdir
#include "toys.h"

GLOBALS(
  char *m, *Z;
)

void mkdir_main(void)
{
  char **s;
  mode_t mode = (0777&~toys.old_umask);

  if (CFG_MKDIR_Z && (toys.optflags&FLAG_Z))
    if (0>lsm_set_create(TT.Z))
      perror_exit("-Z '%s' failed", TT.Z);

  if (TT.m) mode = string_to_mode(TT.m, 0777);

  // Note, -p and -v flags line up with mkpathat() flags
  for (s=toys.optargs; *s; s++) {
    if (mkpathat(AT_FDCWD, *s, mode, toys.optflags|1))
      perror_msg("'%s'", *s);
  }
}
