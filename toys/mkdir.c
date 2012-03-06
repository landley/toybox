/* vi: set sw=4 ts=4:
 *
 * mkdir.c - Make directories
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://pubs.opengroup.org/onlinepubs/009695399/utilities/mkdir.html
 *
 * TODO: Add -m

USE_MKDIR(NEWTOY(mkdir, "<1p", TOYFLAG_BIN))

config MKDIR
	bool "mkdir"
	default y
	help
	  usage: mkdir [-p] [dirname...]
	  Create one or more directories.

	  -p	make parent directories as needed.
*/

#include "toys.h"

DEFINE_GLOBALS(
	long mode;
)

#define TT this.mkdir

static int do_mkdir(char *dir)
{
	unsigned int i;

	if (toys.optflags && *dir) {
		// Skip first char (it can be /)
		for (i = 1; dir[i]; i++) {
			int ret;

			if (dir[i] != '/') continue;
			dir[i] = 0;
			ret = mkdir(dir, TT.mode);
			if (ret < 0 && errno != EEXIST) return ret;
			dir[i] = '/';
		}
	}
	return mkdir(dir, TT.mode);
}

void mkdir_main(void)
{
	char **s;

	TT.mode = 0777;

	for (s=toys.optargs; *s; s++) {
		if (do_mkdir(*s)) {
			perror_msg("cannot create directory '%s'", *s);
			toys.exitval = 1;
		}
	}
}
