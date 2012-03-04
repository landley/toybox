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
	  usage: sleep [.]SECONDS[SUFFIX]

	  Wait a decimal integer number of seconds. If seconds is preceded by .
	  then sleep waits .X seconds. [SUFFIX] can be set to "m" for minutes,
	  "h" for hours or "d" for days. The default suffix is "s" - seconds.
*/

#include "toys.h"

void sleep_main(void)
{
	char *arg = *toys.optargs;
	unsigned long period;
	if (arg[0] == '.') {
		period = (unsigned long)(strtod(arg, NULL) * 1000000);
		toys.exitval = usleep(period);
	} else {
		char suffix = arg[strlen(arg) - 1];
		period = strtoul(arg, NULL, 10);
		switch (suffix) {
			case 'm': period *= 60; break;
			case 'h': period *= 3600; break;
			case 'd': period *= 86400; break;
		}
		toys.exitval = sleep(period);
	}
}
