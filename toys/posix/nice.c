/* nice.c - Run a program at a different niceness level.
 *
 * Copyright 2010 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/nice.html

USE_NICE(NEWTOY(nice, "^<1n#", TOYFLAG_USR|TOYFLAG_BIN))

config NICE
  bool "nice"
  default y
  help
    usage: nice [-n PRIORITY] command [args...]

    Run a command line at an increased or decreased scheduling priority.

    Higher numbers make a program yield more CPU time, from -20 (highest
    priority) to 19 (lowest).  By default processes inherit their parent's
    niceness (usually 0).  By default this command adds 10 to the parent's
    priority.  Only root can set a negative niceness level.
*/

#define FOR_nice
#include "toys.h"

GLOBALS(
  long priority;
)

void nice_main(void)
{
  if (!toys.optflags) TT.priority = 10;

  errno = 0;
  if (nice(TT.priority)==-1 && errno) perror_exit("Can't set priority");

  xexec(toys.optargs);
}
