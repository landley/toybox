/* vi: set sw=4 ts=4:
 * tty.c - print terminal name of stdin
 *
 * Copyright 2007 Charlie Shepherd <masterdriverz@gentoo.org>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/tty.html

USE_TTY(NEWTOY(tty, "s", TOYFLAG_BIN))

config TTY
	bool "tty"
	default y
	help
	  Print the filename of the terminal connected to standard input.

	  -s	Don't print anything, only return an exit status.
*/

#include "toys.h"

void tty_main(void)
{
	char *name = ttyname(0);
	if (!toys.optflags) {
		if (name) puts(name);
		else puts("Not a tty");
	}
	toys.exitval = !name;
}
