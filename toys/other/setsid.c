/* setsid.c - Run program in a new session ID.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>

USE_SETSID(NEWTOY(setsid, "^<1wcd[!dc]", TOYFLAG_USR|TOYFLAG_BIN))

config SETSID
  bool "setsid"
  default y
  help
    usage: setsid [-cdw] command [args...]

    Run process in a new session.

    -d	Detach from tty
    -c	Control tty (become foreground process & receive keyboard signals)
    -w	Wait for child (and exit with its status)
*/

#define FOR_setsid
#include "toys.h"

void setsid_main(void)
{
  int i;

  // This must be before vfork() or tcsetpgrp() will hang waiting for parent.
  setpgid(0, 0);

  // setsid() fails if we're already session leader, ala "exec setsid" from sh.
  // Second call can't fail, so loop won't continue endlessly.
  while (setsid()<0) {
    pid_t pid = XVFORK();

    if (pid) {
      i = 0;
      if (FLAG(w)) {
        i = 127;
        if (pid>0) i = xwaitpid(pid);
      }
      _exit(i);
    }
  }

  if (FLAG(c)) tcsetpgrp(0, getpid());
  if (FLAG(d) && (i = open("/dev/tty", O_RDONLY)) != -1) {
    ioctl(i, TIOCNOTTY);
    close(i);
  }
  xexec(toys.optargs);
}
