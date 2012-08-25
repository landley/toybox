/* vi: set sw=4 ts=4:
 *
 * bzcat.c - decompress stdin to stdout using bunzip2.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.

USE_BZCAT(NEWTOY(bzcat, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config BZCAT
	bool "bzcat"
	default y
	help
	  usage: bzcat [filename...]

	  Decompress listed files to stdout.  Use stdin if no files listed.
*/

#include "toys.h"

static void do_bzcat(int fd, char *name)
{
    bunzipStream(fd, 1);
}

void bzcat_main(void)
{
    loopfiles(toys.optargs, do_bzcat);
}
