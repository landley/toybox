/* pidof.c - Print the Process IDs of all processes with the given names.
 *
 * Copyright 2012 Andreas Heck <aheck@gmx.de>
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/pidof.html

USE_PIDOF(NEWTOY(pidof, "<1so:", TOYFLAG_BIN))

config PIDOF
  bool "pidof"
  default y
  help
    usage: pidof [-s] [-o omitpid[,omitpid...]] [NAME]...

    Print the PIDs of all processes with the given names.

    -s	single shot, only return one pid.
    -o	omit PID(s)
*/

#define FOR_pidof
#include "toys.h"

GLOBALS(
  char *omit;
)

static int print_pid(pid_t pid, char *name)
{
  char * res;
  int len;

  sprintf(toybuf, "%d", (int)pid);
  len = strlen(toybuf);

  // Check omit string
  if (TT.omit && (res = strstr(TT.omit, toybuf)))
    if ((res == TT.omit || res[-1] == ',') &&
      (res[len] == ',' || !res[len])) return 0;

  xprintf("%*s", len+(!toys.exitval), toybuf);
  toys.exitval = 0;

  return toys.optflags & FLAG_s;
}

void pidof_main(void)
{
  toys.exitval = 1;
  names_to_pid(toys.optargs, print_pid);
  if (!toys.exitval) xputc('\n');
}
