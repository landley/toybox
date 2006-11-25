/* vi: set sw=4 ts=4: */
/*
 * cat -v implementation for toybox
 *
 * Copyright (C) 2006 Rob Landley <rob@landley.net>
 */

/* See "Cat -v considered harmful" at
 * http://cm.bell-labs.com/cm/cs/doc/84/kp.ps.gz */

#include "toys.h"

int catv_main(void)
{
	int retval = 0, fd;
	char **argv = toys.optargs;

	toys.optflags^=4;

	// Loop through files.

	do {
		// Read from stdin if there's nothing else to do.

		fd = 0;
		if (*argv && 0>(fd = xopen(*argv, O_RDONLY, 0))) retval = EXIT_FAILURE;
		else for(;;) {
			int i, res;

			res = reread(fd, toybuf, sizeof(toybuf));
			if (res < 0) retval = EXIT_FAILURE;
			if (res < 1) break;
			for (i=0; i<res; i++) {
				char c=toybuf[i];

				if (c > 126 && (toys.optflags & 4)) {
					if (c == 127) {
						printf("^?");
						continue;
					} else {
						printf("M-");
						c -= 128;
					}
				}
				if (c < 32) {
					if (c == 10) {
					   if (toys.optflags & 1) putchar('$');
					} else if (toys.optflags & (c==9 ? 2 : 4)) {
						printf("^%c", c+'@');
						continue;
					}
				}
				putchar(c);
			}
		}
		if (CFG_TOYS_FREE && fd) close(fd);
	} while (*++argv);

	return retval;
}
