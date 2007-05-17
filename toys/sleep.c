/* vi: set sw=4 ts=4: */
/*
 * sleep.c - Wait for a number of seconds.
 */

#include "toys.h"

int sleep_main(void)
{
	return sleep(atol(*toys.optargs));
}
