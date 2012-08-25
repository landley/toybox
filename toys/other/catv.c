/* vi: set sw=4 ts=4:
 *
 * cat -v implementation for toybox
 *
 * Copyright (C) 2006, 2007 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3, but see "Cat -v considered harmful" at
 *   http://cm.bell-labs.com/cm/cs/doc/84/kp.ps.gz

USE_CATV(NEWTOY(catv, "vte", TOYFLAG_USR|TOYFLAG_BIN))

config CATV
	bool "catv"
	default y
	help
	  usage: catv [-evt] [filename...]

	  Display nonprinting characters as escape sequences.  Use M-x for
	  high ascii characters (>127), and ^x for other nonprinting chars.

	  -e	Mark each newline with $
	  -t	Show tabs as ^I
	  -v	Don't use ^x or M-x escapes.
*/

#include "toys.h"

// Callback function for loopfiles()

static void do_catv(int fd, char *name)
{
	for(;;) {
		int i, len;

		len = read(fd, toybuf, sizeof(toybuf));
		if (len < 0) toys.exitval = EXIT_FAILURE;
		if (len < 1) break;
		for (i=0; i<len; i++) {
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
					if (toys.optflags & 1) xputc('$');
				} else if (toys.optflags & (c==9 ? 2 : 4)) {
					printf("^%c", c+'@');
					continue;
				}
			}
			xputc(c);
		}
	}
}

void catv_main(void)
{
	toys.optflags^=4;
	loopfiles(toys.optargs, do_catv);
}
