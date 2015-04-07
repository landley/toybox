/* restorecon.c - Restore default security contexts for files
 *
 * Copyright 2015 The Android Open Source Project

USE_RESTORECON(NEWTOY(restorecon, "<1DFnRrv", TOYFLAG_USR|TOYFLAG_SBIN))

config RESTORECON
  bool "restorecon"
  depends on TOYBOX_SELINUX
  default y
  help
    usage: restorecon [-D] [-F] [-R] [-n] [-v] FILE...

    Restores the default security contexts for the given files.

    -D	apply to /data/data too
    -F	force reset
    -R	recurse into directories
    -n	don't make any changes; useful with -v to see what would change
    -v	verbose: show any changes
*/

#define FOR_restorecon
#include "toys.h"

#if defined(__ANDROID__)
#include <selinux/android.h>
#endif

void restorecon_main(void)
{
#if defined(__ANDROID__)
  char **s;
  int flags = 0;

  if (toys.optflags & FLAG_D) flags |= SELINUX_ANDROID_RESTORECON_DATADATA;
  if (toys.optflags & FLAG_F) flags |= SELINUX_ANDROID_RESTORECON_FORCE;
  if (toys.optflags & (FLAG_R|FLAG_r))
    flags |= SELINUX_ANDROID_RESTORECON_RECURSE;
  if (toys.optflags & FLAG_n) flags |= SELINUX_ANDROID_RESTORECON_NOCHANGE;
  if (toys.optflags & FLAG_v) flags |= SELINUX_ANDROID_RESTORECON_VERBOSE;

  for (s = toys.optargs; *s; s++)
    if (selinux_android_restorecon(*s, flags) < 0)
      perror_msg("restorecon failed: %s", *s);
#endif
}
