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
    usage: sleep SECONDS

    Wait before exiting.

config SLEEP_FLOAT
  bool
  default y
  depends on SLEEP && TOYBOX_FLOAT
  help
    The delay can be a decimal fraction. An optional suffix can be "m"
    (minutes), "h" (hours), "d" (days), or "s" (seconds, the default).
*/

#include "toys.h"

void sleep_main(void)
{

  if (!CFG_TOYBOX_FLOAT) toys.exitval = sleep(atol(*toys.optargs));
  else {
    char *arg;
    double d = strtod(*toys.optargs, &arg);
    struct timespec tv;

    // Parse suffix
    if (*arg) {
      int ismhd[]={1,60,3600,86400};
      char *smhd = "smhd", *c = strchr(smhd, *arg);
      if (!c) error_exit("Unknown suffix '%c'", *arg);
      d *= ismhd[c-smhd];
    }

    tv.tv_nsec=1000000000*(d-(tv.tv_sec = (unsigned long)d));
    toys.exitval = !!nanosleep(&tv, NULL);
  }
}
