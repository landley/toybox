/* vi: set sw=4 ts=4:
 *
 * chroot.c - Run command in new root directory.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.

USE_CHROOT(NEWTOY(chroot, "^<1", TOYFLAG_USR|TOYFLAG_SBIN))

config CHROOT
	bool "chroot"
	default y
	help
	  usage: chroot NEWPATH [commandline...]

	  Run command within a new root directory.  If no command, run /bin/sh.
*/

#include "toys.h"

void chroot_main(void)
{
	char *binsh[] = {"/bin/sh", "-i", 0};
	if (chdir(*toys.optargs) || chroot("."))
		perror_exit("%s", *toys.optargs);
	xexec(toys.optargs[1] ? toys.optargs+1 : binsh);
}
