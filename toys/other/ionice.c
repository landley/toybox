/* ionice.c - set or get process I/O scheduling class and priority
 *
 * Copyright 2015 Rob Landley <rob@landley.net>
 *
 * It would be really nice if there was a standard, but no. There is
 * Documentation/block/ioprio.txt in the linux source.

USE_IONICE(NEWTOY(ionice, "^tc#<0>3=2n#<0>7=5p#", TOYFLAG_USR|TOYFLAG_BIN))
USE_IORENICE(NEWTOY(iorenice, "?<1>3", TOYFLAG_USR|TOYFLAG_BIN))

config IONICE
  bool "ionice"
  default y
  help
    usage: ionice [-t] [-c CLASS] [-n LEVEL] [COMMAND...|-p PID]

    Change the I/O scheduling priority of a process. With no arguments
    (or just -p), display process' existing I/O class/priority.

    -c	CLASS = 1-3: 1(realtime), 2(best-effort, default), 3(when-idle)
    -n	LEVEL = 0-7: (0 is highest priority, default = 5)
    -p	Affect existing PID instead of spawning new child
    -t	Ignore failure to set I/O priority

    System default iopriority is generally -c 2 -n 4.

config IORENICE
  bool "iorenice"
  default y
  help
    usage: iorenice PID [CLASS] [PRIORITY]

    Display or change I/O priority of existing process. CLASS can be
    "rt" for realtime, "be" for best effort, "idle" for only when idle, or
    "none" to leave it alone. PRIORITY can be 0-7 (0 is highest, default 4).
*/

#define FOR_ionice
#include "toys.h"
#include <sys/syscall.h>

GLOBALS(
  long pid;
  long level;
  long class;
)

static int ioprio_get(void)
{
  return syscall(__NR_ioprio_get, 1, (int)TT.pid);
}

static int ioprio_set(void)
{
  int prio = ((int)TT.class << 13) | (int)TT.level;

  return syscall(__NR_ioprio_set, 1, (int)TT.pid, prio);
}

void ionice_main(void)
{
  if (!TT.pid && !toys.optc) error_exit("Need -p or COMMAND");
  if (toys.optflags == FLAG_p) {
    int p = ioprio_get();
    xprintf("%s: prio %d\n",
      (char *[]){"unknown", "Realtime", "Best-effort", "Idle"}[(p>>13)&3],
      p&7);
  } else {
    if (-1 == ioprio_set() && !(toys.optflags&FLAG_t)) perror_exit("set");
    if (!TT.pid) xexec(toys.optargs);
  }
}

void iorenice_main(void)
{
  char *classes[] = {"none", "rt", "be", "idle"};

  TT.pid = atolx(*toys.optargs);
  if (toys.optc == 1) {
    int p = ioprio_get();

    if (p == -1) perror_exit("read priority");
    TT.class = (p>>13)&3;
    p &= 7;
    xprintf("Pid %ld, class %s (%ld), prio %d\n",
            TT.pid, classes[TT.class], TT.class, p);
    return;
  }

  for (TT.class = 0; TT.class<4; TT.class++)
    if (!strcmp(toys.optargs[toys.optc-1], classes[TT.class])) break;
  if (toys.optc == 3 || TT.class == 4) TT.level = atolx(toys.optargs[1]);
  else TT.level = 4;
  TT.class &= 3;

  if (-1 == ioprio_set()) perror_exit("set");
}
