/* vi: set sw=4 ts=4: */
/*
 * cat -v implementation for toybox
 *
 * Copyright (C) 2006 Rob Landley <rob@landley.net>
 */

/* See "Cat -v considered harmful" at
 * http://cm.bell-labs.com/cm/cs/doc/84/kp.ps.gz */

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
