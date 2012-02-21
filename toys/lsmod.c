/* vi: set sw=4 ts=4:
 *
 * lsmod.c - Show the status of modules in the kernel
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * Not in SUSv4.

USE_LSMOD(NEWTOY(lsmod, NULL, TOYFLAG_BIN))

config LSMOD
	bool "lsmod"
	default y
	help
	  usage: lsmod

	  Display the currently loaded modules, their sizes and their
	  dependencies.
*/

#include "toys.h"

void lsmod_main(void)
{
	FILE * file = xfopen("/proc/modules", "r");
	char *name, *size, *refcnt, *users;

	xprintf("%-24s Size  Used by\n", "Module");

	while (fgets(toybuf, sizeof(toybuf), file)) {
		int len;

		name = strtok(toybuf, " ");
		size = strtok(NULL, " ");
		refcnt = strtok(NULL, " ");
		users = strtok(NULL, " ");

		if(name && size && refcnt && users) {
			len = strlen(users)-1;
			if (users[len] == ',' || users[len] == '-')
				users[len] = 0;
			xprintf("%-20s %8s  %s %s\n", name, size, refcnt, users);
		} else perror_exit("unrecognized input");
	}
	fclose(file);
}
