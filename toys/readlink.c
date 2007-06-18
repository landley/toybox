/* vi: set sw=4 ts=4: */
/*
 * readlink.c - Return string representation of a symbolic link.
 */
// Note: Hardware in LINK_MAX as 127 since it was removed from glibc.

#include "toys.h"

int readlink_main(void)
{
	char *s = xreadlink(*toys.optargs);

	if (s) {
		xputs(s);
		if (CFG_TOYBOX_FREE) free(s);
		return 0;
	}

	return 1;
}
