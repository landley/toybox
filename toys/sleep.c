/* vi: set sw=4 ts=4:
 *
 * sleep.c - Wait for a number of seconds.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/sleep.html

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
		unsigned long l;

		// Parse suffix
		if (*arg) {
			int imhd[]={60,3600,86400};
			char *mhd = "mhd", *c = strchr(mhd, *arg);
			if (!arg) error_exit("Unknown suffix '%c'", *arg);
			d *= imhd[c-mhd];
		}

		// wait through the delay
		l = (unsigned long)d;
		d -= l;
		if (l) toys.exitval = sleep(l);
		if (!toys.exitval)
			toys.exitval = nanosleep((unsigned long)(d * 1000000000));
	}
}
