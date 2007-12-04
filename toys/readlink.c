/* vi: set sw=4 ts=4: */
/*
 * readlink.c - Return string representation of a symbolic link.
 *
 * Not in SUSv3.
 */

#include "toys.h"

void readlink_main(void)
{
	char *s = xreadlink(*toys.optargs);

	if (s) {
		xputs(s);
		if (CFG_TOYBOX_FREE) free(s);
	} else toys.exitval = 1;
}
