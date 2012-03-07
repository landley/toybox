/* vi: set sw=4 ts=4:
 *
 * mkfifo.c - Create FIFOs (named pipes)
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://pubs.opengroup.org/onlinepubs/009695399/utilities/mkfifo.html
 *
 * TODO: Add -m

USE_MKFIFO(NEWTOY(mkfifo, "<1", TOYFLAG_BIN))

config MKFIFO
	bool "mkfifo"
	default y
	help
	  usage: mkfifo [fifo_name...]
	  Create FIFOs (named pipes).

*/

#include "toys.h"

void mkfifo_main(void)
{
	char **s;
	mode_t mode = 0666;
	for (s = toys.optargs; *s; s++) {
		if (mknod(*s, S_IFIFO | mode, 0) < 0) {
			fprintf(stderr, "mkfifo: cannot create fifo `%s': %s\n", *s, strerror(errno));
			toys.exitval = 1;
		}
	}
}
