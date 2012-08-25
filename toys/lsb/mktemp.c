/* vi: set sw=4 ts=4:
 *
 * mktemp.c - Create a temporary file or directory.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * Not in SUSv4.

USE_MKTEMP(NEWTOY(mktemp, ">1(directory)d(tmpdir)p:", TOYFLAG_BIN))

config MKTEMP
	bool "mktemp"
	default y
	help
	  usage: mktemp [OPTION] [TEMPLATE]

	  Safely create a temporary file or directory and print its name.
	  TEMPLATE should end in 6 consecutive X's, the default
	  template is tmp.XXXXXX and the default directory is /tmp/.
	  
	  -d, --directory        Create a directory, instead of a file
	  -p DIR, --tmpdir=DIR   Use DIR as a base path

*/

#include "toys.h"

DEFINE_GLOBALS(
	char * tmpdir;
)
#define TT this.mktemp

void mktemp_main(void)
{
	int  d_flag = toys.optflags & 2;
	char *tmp, *path;

	tmp = *toys.optargs;
	if (!tmp) tmp = "tmp.XXXXXX";
	if (!TT.tmpdir) TT.tmpdir = "/tmp/";

	tmp = xmsprintf("%s/%s", TT.tmpdir, tmp);

	if (d_flag ? mkdtemp(tmp) == NULL : mkstemp(tmp) == -1)
		perror_exit("Failed to create temporary %s",
			d_flag ? "directory" : "file");

	xputs(path = xrealpath(tmp));

	if (CFG_TOYBOX_FREE) {
		free(path);
		free(tmp);
	}
}
