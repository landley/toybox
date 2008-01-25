/* vi: set sw=4 ts=4:
 *
 * dirname.c - print directory portion of path, or "." if none.
 *
 * Copyright 2007 Charlie Shepherd <masterdriverz@gentoo.org>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/dirname.html

USE_DIRNAME(NEWTOY(dirname, "<1>1", TOYFLAG_BIN))

config DIRNAME
	bool "dirname"
	default y
	help
	  usage: dirname path

	  Print the part of path up to the last slash.
*/

#include "toys.h"
#include <libgen.h>

void dirname_main(void)
{
	puts(dirname(*toys.optargs));
}
