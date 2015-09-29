/* setsid.c - Run program in a new session ID.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>

USE_SETSID(NEWTOY(setsid, "^<1t", TOYFLAG_USR|TOYFLAG_BIN))

config SETSID
  bool "setsid"
  default y
  help
    usage: setsid [-t] command [args...]

    Run process in a new session.

    -t	Grab tty (become foreground process, receiving keyboard signals)
*/

#include "toys.h"

void setsid_main(void)
{
  while (setsid()<0) if (XVFORK()) _exit(0);
  if (toys.optflags) {
    setpgid(0, 0);
    tcsetpgrp(0, getpid());
  }
  xexec(toys.optargs);
}
