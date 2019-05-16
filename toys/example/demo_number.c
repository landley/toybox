/* demo_number.c - Expose atolx() and human_readable() for testing.
 *
 * Copyright 2015 Rob Landley <rob@landley.net>

USE_DEMO_NUMBER(NEWTOY(demo_number, "D#=3<3hdbs", TOYFLAG_BIN))

config DEMO_NUMBER
  bool "demo_number"
  default n
  help
    usage: demo_number [-hsbi] NUMBER...

    -b	Use "B" for single byte units (HR_B)
    -d	Decimal units
    -h	Human readable
    -s	Space between number and units (HR_SPACE)
*/

#define FOR_demo_number
#include "toys.h"

GLOBALS(
  long D;
)

void demo_number_main(void)
{
  char **arg;

  for (arg = toys.optargs; *arg; arg++) {
    long long ll = atolx(*arg);

    if (toys.optflags) {
      human_readable_long(toybuf, ll, TT.D, toys.optflags);
      xputs(toybuf);
    } else printf("%lld\n", ll);
  }
}
