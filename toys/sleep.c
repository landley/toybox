/* vi: set sw=4 ts=4: */
/*
 * sleep.c - Wait for a number of seconds.
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/sleep.html
 */

#include "toys.h"

void sleep_main(void)
{
	toys.exitval = sleep(atol(*toys.optargs));
}
