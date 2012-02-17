/* vi: set sw=4 ts=4:
 *
 * realpath.c - Return the canonical version of a pathname
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * Not in SUSv4.

USE_REALPATH(NEWTOY(realpath, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config REALPATH
	bool "realpath"
	default y
	help
	  Display the canonical absolute pathname
*/

#include "toys.h"

static void do_realpath(int fd, char *name)
{
    if (!realpath(name, toybuf))
        perror_exit("realpath: cannot access %s'", name);

    xprintf("%s\n", toybuf);
}

void realpath_main(void)
{
    loopfiles(toys.optargs, do_realpath);
}
