/* vi: set sw=4 ts=4:
 *
 * truncate.c - set file length, extending sparsely if necessary
 *
 * Copyright 2011 Rob Landley <rob@landley.net>

USE_TRUNCATE(NEWTOY(truncate, "<1s#|c", TOYFLAG_BIN))

config TRUNCATE
	bool "truncate"
	default y
	help
	  usage: truncate [-c] -s file...
	  Set length of file(s), extending sparsely if necessary.

	  -c	Don't create file if it doesn't exist.
	  -s	New size
*/

#include "toys.h"

DEFINE_GLOBALS(
	long size;
)

#define TT this.truncate

static void do_truncate(int fd, char *name)
{
	if (fd<0) return;
	if (ftruncate(fd, TT.size)) {
		perror_msg("failed to set '%s' to '%ld'", name, TT.size);
		toys.exitval = EXIT_FAILURE;
	}
}

void truncate_main(void)
{
	int cr = !(toys.optflags&1);

	// Create files with mask rwrwrw.
	// Nonexistent files are only an error if we're supposed to create them.
	loopfiles_rw(toys.optargs, O_WRONLY|(cr ? O_CREAT : 0), 0666, cr,
		do_truncate);
}
