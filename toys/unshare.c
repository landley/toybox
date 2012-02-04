/* vi: set sw=4 ts=4:
 *
 * unshare.c - run command in new context
 *
 * Copyright 2011 Rob Landley <rob@landley.net>
 *
 * Not in SUSv4.

USE_UNSHARE(NEWTOY(unshare, "<1^nium", TOYFLAG_USR|TOYFLAG_BIN))

config UNSHARE
	bool "unshare"
	default y
	depends on TOYBOX_CONTAINER
	help
	  usage: unshare [-muin] COMMAND...

	  Create new namespace(s) for this process and its children, so some
	  attribute is not shared with the parent process.  This is part of
	  Linux Containers.  Each process can have its own:

	  -m	Mount/unmount tree
	  -u	Host and domain names
	  -i	SysV IPC (message queues, semaphores, shared memory)
	  -n	Network address, sockets, routing, iptables
*/

#include "toys.h"

#include <sched.h>

void unshare_main(void)
{
	unsigned flags[]={CLONE_NEWNS, CLONE_NEWUTS, CLONE_NEWIPC, CLONE_NEWNET,0};
	unsigned f=0;
	int i;

	for (i=0; flags[i]; i++)
		if (toys.optflags & (1<<i))
			f |= flags[i];

	if(unshare(f)) perror_exit("failed");

	xexec(toys.optargs);
}
