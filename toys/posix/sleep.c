/* sleep.c - Wait for a number of seconds.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/sleep.html

USE_SLEEP(NEWTOY(sleep, "<1", TOYFLAG_BIN))

config SLEEP
  bool "sleep"
  default y
  help
    usage: sleep LENGTH

    Wait before exiting. An optional suffix can be "m" (minutes), "h" (hours),
    "d" (days), or "s" (seconds, the default).


config SLEEP_FLOAT
  bool
  default y
  depends on SLEEP && TOYBOX_FLOAT
  help
    Length can be a decimal fraction.
*/

#include "toys.h"

void sleep_main(void)
{
  struct timespec tv;

  tv.tv_sec = xparsetime(*toys.optargs, 1000000000, &tv.tv_nsec);
  toys.exitval = !!nanosleep(&tv, NULL);
}
