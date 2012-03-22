/* vi: set sw=4 ts=4:
 *
 * comm.c - select or reject lines common to two files
 *
 * Copyright 2012 Ilya Kuzmich <ikv@safe-mail.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/009695399/utilities/comm.html

USE_COMM(NEWTOY(comm, "123", TOYFLAG_USR|TOYFLAG_BIN))

config COMM
	bool "comm"
	default n
	help
	  usage: comm [-123] FILE1 FILE2

	  Reads FILE1 and FILE2, which should be ordered, and produces three text
	  columns as output: lines only in FILE1; lines only in FILE2; and lines
	  in both files. Filename "-" is a synonym for stdin.

	  -1 suppress the output column of lines unique to FILE1
	  -2 suppress the output column of lines unique to FILE2
	  -3 suppress the output column of lines duplicated in FILE1 and FILE2
*/

#include "toys.h"

#define FLAG_SUPPRESS_3 1
#define FLAG_SUPPRESS_2 2
#define FLAG_SUPPRESS_1 4

static void writeline(const char *line, int col)
{
	if (col == 0 && toys.optflags & FLAG_SUPPRESS_1) return;
	else if (col == 1) {
		if (toys.optflags & FLAG_SUPPRESS_2) return;
		if (!(toys.optflags & FLAG_SUPPRESS_1)) putchar('\t');
	} else if (col == 2) {
		if (toys.optflags & FLAG_SUPPRESS_3) return;
		if (!(toys.optflags & FLAG_SUPPRESS_1)) putchar('\t');
		if (!(toys.optflags & FLAG_SUPPRESS_2)) putchar('\t');
	}
	puts(line);
}

void comm_main(void)
{
	int file[2];
	char *line[2];
	int i;

	if (toys.optc != 2)
		perror_exit("exactly 2 operands required");

	if (toys.optflags == (FLAG_SUPPRESS_1 | FLAG_SUPPRESS_2 | FLAG_SUPPRESS_3))
		return;

	for (i = 0; i < 2; i++) {
		file[i] = strcmp("-", toys.optargs[i]) ? xopen(toys.optargs[i], O_RDONLY) : 0;
		line[i] = get_line(file[i]);
	}

	while (line[0] && line[1]) {
		int order = strcmp(line[0], line[1]);

		if (order == 0) {
			writeline(line[0], 2);
			for (i = 0; i < 2; i++) {
				free(line[i]);
				line[i] = get_line(file[i]);
			}
		} else {
			i = order < 0 ? 0 : 1;
			writeline(line[i], i);
			free(line[i]);
			line[i] = get_line(file[i]);
		}
	}

	/* print rest of the longer file */
	for (i = line[0] ? 0 : 1; line[i];) {
		writeline(line[i], i);
		free(line[i]);
		line[i] = get_line(file[i]);
	}

	if (CFG_TOYBOX_FREE) {
		for (i = 0; i < 2; i--)
			xclose(file[i]);
	}
}
