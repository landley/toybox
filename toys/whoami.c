/* vi: set sw=4 ts=4:
 *
 * whoami.c - Print effective user id
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *

USE_WHOAMI(NEWTOY(whoami, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config WHOAMI
	bool "whoami"
	default y
	help
	  usage: whoami

	  Print effective user id.
*/

#include "toys.h"

void whoami_main(void)
{
	struct passwd *pw = getpwuid(geteuid());

	if (!pw) {
		perror("getpwuid");
		toys.exitval = 1;
		return;
	}

	xprintf("%s\n", pw->pw_name);
}
