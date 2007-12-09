/* oneit.c, tiny one-process init replacement.
 *
 * Copyright 2005, 2007 by Rob Landley <rob@landley.net>.
 *
 * Not in SUSv3.
 */

#include "toys.h"
#include <sys/reboot.h>

// The minimum amount of work necessary to get ctrl-c and such to work is:
//
// - Fork a child (PID 1 is special: can't exit, has various signals blocked).
// - Do a setsid() (so we have our own session).
// - In the child, attach stdio to /dev/tty0 (/dev/console is special)
// - Exec the rest of the command line.
//
// PID 1 then reaps zombies until the child process it spawned exits, at which
// point it calls sync() and reboot().  I could stick a kill -1 in there.

#define TT toy.oneit

void oneit_main(void)
{
  int i;
  pid_t pid;

  // Create a new child process.
  pid = vfork();
  if (pid) {

    // pid 1 just reaps zombies until it gets its child, then halts the system.
    while (pid!=wait(&i));
    sync();
    reboot(toys.optflags ? RB_POWER_OFF : RB_AUTOBOOT);
  }

  // Redirect stdio to /dev/tty0, with new session ID, so ctrl-c works.
  setsid();
  for (i=0; i<3; i++) {
    close(i);
    xopen(TT.console ? TT.console : "/dev/tty0",O_RDWR);
  }

  // Can't xexec() here, because we vforked so we don't want to error_exit().
  toy_exec(toys.optargs);
  execvp(*toys.optargs, toys.optargs);
  _exit(127);
}
