/* vi: set sw=4 ts=4:
 *
 * uniq.c - report or filter out repeated lines in a file
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/uniq.html

USE_UNIQ(NEWTOY(uniq, "f#s#w#zicdu", TOYFLAG_BIN))

config UNIQ
	bool "uniq"
	default y
	help
	  usage: uniq [-cduiz] [-w maxchars] [-f fields] [-s char] [input_file [output_file]]

	  Report or filter out repeated lines in a file

	  -c	show counts before each line
	  -d	show only lines that are repeated
	  -u	show only lines that are unique
	  -i	ignore case when comparing lines
	  -z	lines end with \0 not \n
	  -w	compare maximum X chars per line
	  -f	ignore first X fields
	  -s	ignore first X chars
*/

#include "toys.h"

DEFINE_GLOBALS(
	long maxchars;
	long nchars;
	long nfields;
	long repeats;
)

#define TT this.uniq

#define FLAG_z 16
#define FLAG_i 8
#define FLAG_c 4
#define FLAG_d 2
#define FLAG_u 1

static char *skip(char *str)
{
	int field = 0;
	long nchars = TT.nchars;
	long nfields = TT.nfields;
	// Skip fields first
	while (nfields && *str) {
		if (isspace((unsigned char)*str)) {
			if (field) {
				field = 0;
				nfields--;
			}
		} else if (!field) {
			field = 1;
		}
		str++;
	}
	// Skip chars
	while (nchars-- && *str)
		str++;
	return str;
}

static void print_line(FILE *f, char *line)
{
	if (TT.repeats == 0 && (toys.optflags & FLAG_d))
		return;
	if (TT.repeats > 0 && (toys.optflags & FLAG_u))
		return;
	if ((toys.optflags & FLAG_c)) {
		fprintf(f, "%7lu %s", TT.repeats + 1, line);
	} else {
		fprintf(f, "%s", line);
	}
	if (toys.optflags & FLAG_z)
		fprintf(f, "%c", '\0');
}

void uniq_main(void)
{
	FILE *infile = stdin;
	FILE *outfile = stdout;
	char *thisline = NULL;
	char *prevline = NULL;
	size_t thissize, prevsize = 0;
	char *tmpline;
	char eol = '\n';
	size_t tmpsize;

	if (toys.optc >= 1)
		infile = xfopen(toys.optargs[0], "r");

	if (toys.optc >= 2)
		outfile = xfopen(toys.optargs[1], "w");

	if (toys.optflags & FLAG_z)
		eol = '\0';

	// If first line can't be read
	if (getdelim(&prevline, &prevsize, eol, infile) < 0)
		return;

	while (getdelim(&thisline, &thissize, eol, infile) > 0) {
		int diff;
		char *t1, *t2;

		// If requested get the chosen fields + character offsets.
		if (TT.nfields || TT.nchars) {
			t1 = skip(thisline);
			t2 = skip(prevline);
		} else {
			t1 = thisline;
			t2 = prevline;
		}

		if (TT.maxchars == 0) {
			diff = !(toys.optflags & FLAG_i)
			        ? strcmp(t1, t2)
			        : strcasecmp(t1, t2);
		} else {
			diff = !(toys.optflags & FLAG_i)
			        ? strncmp(t1, t2, TT.maxchars)
			        : strncasecmp(t1, t2, TT.maxchars);
		}

		if (diff == 0) { // same
			TT.repeats++;
		} else {
			print_line(outfile, prevline);

			TT.repeats = 0;

			tmpline = prevline;
			prevline = thisline;
			thisline = tmpline;

			tmpsize = prevsize;
			prevsize = thissize;
			thissize = tmpsize;
		}
	}

	print_line(outfile, prevline);

	if (CFG_TOYBOX_FREE) {
		free(prevline);
		free(thisline);
	}
}
