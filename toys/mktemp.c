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
	int  p_flag = (toys.optflags & 1);
	int  d_flag = (toys.optflags & 2) >> 1;
	char * result;

	int size = snprintf(toybuf, sizeof(toybuf)-1, "%s/%s",
			(p_flag && TT.tmpdir)?TT.tmpdir:"/tmp/",
			(toys.optargs[0])?toys.optargs[0]:"tmp.XXXXXX");
	toybuf[size] = 0;

	if (d_flag) {
		if (mkdtemp(toybuf) == NULL)
			perror_exit("Failed to create temporary directory");
	} else {
		if (mkstemp(toybuf) == -1)
			perror_exit("Failed to create temporary file");
	}

	result = realpath(toybuf, NULL);
	xputs(result);

	if (CFG_TOYBOX_FREE)
		free(result);
}
