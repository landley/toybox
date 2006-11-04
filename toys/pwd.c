/* vi: set sw=4 ts=4: */
/*
 * pwd.c - Print working directory.
 */

#include "toys.h"

int pwd_main(void)
{
	char *pwd = xgetcwd();

	puts(pwd);
	if (CFG_TOYS_FREE) free(pwd);

	return 0;
}
