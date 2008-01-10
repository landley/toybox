/* vi: set sw=4 ts=4: */
/*
 * sed.c - Stream editor.
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/sed.c
 */

#include "toys.h"
#include "lib/xregcomp.h"

#define TT toy.sed

struct sed_command {
	// Doubly linked list of commands.
	struct sed_command *next, *prev;

	// Regexes for s/match/data/ and /match_begin/,/match_end/command
	regex_t *match, *match_begin, *match_end;

	// For numeric ranges ala 10,20command
	int first_line, last_line;

	// Which match to replace, 0 for all.
	int which;

	// s and w commands can write to a file.  Slight optimization: we use 0
	// instead of -1 to mean no file here, because even when there's no stdin
	// our input file would take fd 0.
	int outfd;

	// Data string for (saicytb)
	char *data;

	// Which command letter is this?
	char command;
};

void sed_main(void)
{
	struct arg_list *test;

	for (test = TT.commands; test; test = test->next)
		dprintf(2,"command=%s\n",test->arg);

	printf("Hello world\n");
}
