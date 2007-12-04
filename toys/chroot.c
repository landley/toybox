/* vi: set sw=4 ts=4: */
/*
 * chroot.c - Run command in new root directory.
 *
 * Not in SUSv3.
 */

#include "toys.h"

void chroot_main(void)
{
	char *binsh[] = {"/bin/sh", "-i", 0};
	if (chdir(*toys.optargs) || chroot("."))
		perror_exit("%s", *toys.optargs);
	xexec(toys.optargs[1] ? toys.optargs+1 : binsh);
}
