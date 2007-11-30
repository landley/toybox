/* vi: set sw=4 ts=4: */
/*
 * pwd.c - Print working directory.
 */

#include "toys.h"

void pwd_main(void)
{
	char *pwd = xgetcwd();

	xprintf("%s\n", pwd);
	if (CFG_TOYBOX_FREE) free(pwd);
}
