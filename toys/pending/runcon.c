/* runcon.c - Run command in specified security context
 *
 * Copyright 2015 The Android Open Source Project

USE_RUNCON(NEWTOY(runcon, "<2", TOYFLAG_USR|TOYFLAG_SBIN))

config RUNCON
  bool "runcon"
  depends on TOYBOX_SELINUX
  default n
  help
    usage: runcon CONTEXT COMMAND [ARGS...]

    Run a command in a specified security context.
*/

#define FOR_runcon
#include "toys.h"

void runcon_main(void)
{
  char *context = *toys.optargs;

  if (setexeccon(context))
    error_exit("Could not set context to %s: %s", context, strerror(errno));

  toys.optargs++;
  xexec(toys.optargs);
}
