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
	struct stat buf;
	char *s;

	// mkdir -p one/two/three is not an error if the path already exists,
	// but is if "three" is a file.  The others we dereference and catch
	// not-a-directory along the way, but the last one we must explicitly
	// test for. Might as well do it up front.

	if (!stat(dir, &buf) && !S_ISDIR(buf.st_mode)) {
		errno = EEXIST;
		return 1;
	}

	for (s=dir; ; s++) {
		char save=0;

		// Skip leading / of absolute paths.
		if (s!=dir && *s == '/' && toys.optflags) {
			save = *s;
			*s = 0;
		} else if (*s) continue;

		if (mkdir(dir, TT.mode)<0 && (!toys.optflags || errno != EEXIST))
			return 1;

		if (!(*s = save)) break;
	}

	return 0;
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
