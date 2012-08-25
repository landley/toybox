/* vi: set sw=4 ts=4:
 *
 * rmmod.c - Remove a module from the Linux kernel.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_RMMOD(NEWTOY(rmmod, "<1wf", TOYFLAG_BIN|TOYFLAG_NEEDROOT))

config RMMOD
	bool "rmmod"
	default y
	help
	  usage: rmmod [-wf] [MODULE]

	  Unload the module named MODULE from the Linux kernel.
	  -f   Force unload of a module
	  -w   Wait until the module is no longer used.

*/

#include "toys.h"

#include <sys/syscall.h>
#define delete_module(mod, flags) syscall(__NR_delete_module, mod, flags)

void rmmod_main(void)
{
	unsigned int flags = O_NONBLOCK|O_EXCL;
	char * mod_name;
	int len;

	// Basename
	mod_name = strrchr(toys.optargs[0],'/');
	if (mod_name)
		mod_name++;
	else
		mod_name = toys.optargs[0];

	// Remove .ko if present
	len = strlen(mod_name);
	if (len > 3 && !strcmp(&mod_name[len-3], ".ko" ))
		mod_name[len-3] = 0;

	if (toys.optflags & 1) flags |= O_TRUNC;
	if (toys.optflags & 2) flags &= ~O_NONBLOCK;

	if (delete_module(mod_name, flags))
		perror_exit("failed to unload %s", mod_name);
}
