/* vi: set sw=4 ts=4:
 *
 * wc.c - Word count
 *
 * Copyright 2011 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/wc.html

USE_WC(NEWTOY(wc, "cwl", TOYFLAG_USR|TOYFLAG_BIN))

config WC
	bool "wc"
	default y
	help
	  usage: wc -lwc [FILE...]

	  Count lines, words, and characters in input.

	  -l	show lines
	  -w	show words
	  -c	show characters

	  By default outputs lines, words, characters, and filename for each
	  argument (or from stdin if none).
*/

#include "toys.h"

DEFINE_GLOBALS(
	unsigned long totals[3];
)

#define TT this.wc

static void show_lengths(unsigned long *lengths, char *name)
{
	int i, nospace = 1;
	for (i=0; i<3; i++) {
		if (!toys.optflags || (toys.optflags&(1<<i))) {
			xprintf(" %ld"+nospace, lengths[i]);
			nospace = 0;
		}
		TT.totals[i] += lengths[i];
	}
	if (*toys.optargs) xprintf(" %s", name);
	xputc('\n');
}

static void do_wc(int fd, char *name)
{
	int i, len;
	unsigned long word=0, lengths[]={0,0,0};

	for (;;) {
		len = read(fd, toybuf, sizeof(toybuf));
		if (len<0) {
			perror_msg("%s",name);
			toys.exitval = EXIT_FAILURE;
		}
		if (len<1) break;
		for (i=0; i<len; i++) {
			if (toybuf[i]==10) lengths[0]++;
			if (isspace(toybuf[i])) word=0;
			else {
				if (!word) lengths[1]++;
				word=1;
			}
			lengths[2]++;
		}
	}

	show_lengths(lengths, name);
}

void wc_main(void)
{
	loopfiles(toys.optargs, do_wc);
	if (toys.optc>1) show_lengths(TT.totals, "total");
}
