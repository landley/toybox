/* vi: set sw=4 ts=4:
 *
 * readlink.c - Return string representation of a symbolic link.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.

USE_READLINK(NEWTOY(readlink, "<1f", TOYFLAG_BIN))

config READLINK
	bool "readlink"
	default n
	help
	  usage: readlink

	  Show what a symbolic link points to.

config READLINK_F
	bool "readlink -f"
	default n
	depends on READLINK
	help
	  usage: readlink [-f]

	  -f	Show full cannonical path, with no symlinks in it.  Returns
		nonzero if nothing could currently exist at this location.
*/

#include "toys.h"

void readlink_main(void)
{
	char *s;

	// Calculating full cannonical path?

	if (CFG_READLINK_F && toys.optflags) s = realpath(*toys.optargs, NULL);
	else s = xreadlink(*toys.optargs);

	if (s) {
		xputs(s);
		if (CFG_TOYBOX_FREE) free(s);
	} else toys.exitval = 1;
}
