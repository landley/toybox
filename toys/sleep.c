/* vi: set sw=4 ts=4:
 *
 * sleep.c - Wait for a number of seconds.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/sleep.html

USE_SLEEP(NEWTOY(sleep, "<1", TOYFLAG_BIN))

config SLEEP
	bool "sleep"
	default y
	help
	  usage: sleep SECONDS

	  Wait a decimal integer number of seconds.
*/

#include "toys.h"

DEFINE_GLOBALS(
	long seconds;
)

void sleep_main(void)
{
	toys.exitval = sleep(atol(*toys.optargs));
}
