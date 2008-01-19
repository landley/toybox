/* vi: set sw=4 ts=4:
 *
 * pwd.c - Print working directory.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/echo.html
 *
 * TODO: add -L -P

USE_PWD(NEWTOY(pwd, NULL, TOYFLAG_BIN))

config PWD
	bool "pwd"
	default y
	help
	  usage: pwd

	  The print working directory command prints the current directory.
*/

#include "toys.h"

void pwd_main(void)
{
	char *pwd = xgetcwd();

	xprintf("%s\n", pwd);
	if (CFG_TOYBOX_FREE) free(pwd);
}
