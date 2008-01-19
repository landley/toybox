/* vi: set sw=4 ts=4:
 *
 * basename.c - print non-directory portion of path
 *
 * Copyright 2007 Charlie Shepherd <masterdriverz@gentoo.org>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/basename.html

USE_BASENAME(NEWTOY(basename, "<1>2", TOYFLAG_BIN))

config BASENAME
	bool "basename"
	default y
	help
	  usage: basename path [suffix]

	  Print the part of path after the last slash, optionally minus suffix.
*/

#include "toys.h"

void basename_main(void)
{
	char *name = basename(*toys.optargs);
	char *suffix = toys.optargs[1];
	if (suffix) {
		char *end = name+strlen(name)-strlen(suffix);
		if (end>name && !strcmp(end,suffix)) *end=0;
	}
	puts(name);
}
