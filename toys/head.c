/* vi: set sw=4 ts=4:
 *
 * head.c - copy first lines from input to stdout.
 *
 * Copyright 2006 Timothy Elliott <tle@holymonkey.com>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/head.html

USE_HEAD(NEWTOY(head, "n#", TOYFLAG_BIN))

config HEAD
	bool "head"
	default y
	help
	  usage: head [-n number] [file...]
	  Copy first lines from files to stdout. If no files listed, copy from
	  stdin. Filename "-" is a synonym for stdin.

	  -n	Number of lines to copy.
*/

#include "toys.h"

DEFINE_GLOBALS(
	long lines;
	int file_no;
)

#define TT this.head

static void do_head(int fd, char *name)
{
	int i, len, lines=TT.lines, size=sizeof(toybuf);

	if (toys.optc > 1) {
		// Print an extra newline for all but the first file
		if (TT.file_no++ > 0) printf("\n");
		printf("==> %s <==\n", name);
	}

	for (;lines>0;) {
		len = read(fd, toybuf, size);
		if (len<0) {
			perror_msg("%s",name);
			toys.exitval = EXIT_FAILURE;
		}
		if (len<1) break;
		
		for(i=0; i<len; i++) {
			if (toybuf[i] == '\n' && --lines < 1) break;
		}
		xwrite(1, toybuf, i+1);
	}
}

void head_main(void)
{
	if (!toys.optflags) TT.lines = 10;
	if (TT.lines < 0) perror_exit("Invalid number of lines.");
	TT.file_no = 0;
	loopfiles(toys.optargs, do_head);
}
