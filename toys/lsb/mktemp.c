/* vi: set sw=4 ts=4:
 *
 * mktemp.c - Create a temporary file or directory.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/mktemp.html

USE_MKTEMP(NEWTOY(mktemp, ">1q(directory)d(tmpdir)p:", TOYFLAG_BIN))

config MKTEMP
	bool "mktemp"
	default y
	help
	  usage: mktemp [-dq] [-p DIR] [TEMPLATE]

	  Safely create new file and print its name. Default TEMPLATE is
	  /tmp/tmp.XXXXXX and each trailing X is replaced with random char.

	  -d, --directory        Create directory instead of file
	  -p DIR, --tmpdir=DIR   Put new file in DIR
	  -q                     Quiet
*/

#include "toys.h"

DEFINE_GLOBALS(
	char * tmpdir;
)

#define FLAG_p 1
#define FLAG_d 2
#define FLAG_q 4

#define TT this.mktemp

void mktemp_main(void)
{
	int  d_flag = toys.optflags & FLAG_d;
	char *tmp;

	tmp = *toys.optargs;

	if (!tmp) {
		if (!TT.tmpdir) TT.tmpdir = "/tmp";
		tmp = "tmp.xxxxxx";
	}
	if (TT.tmpdir) tmp = xmsprintf("%s/%s", TT.tmpdir ? TT.tmpdir : "/tmp",
		*toys.optargs ? *toys.optargs : "tmp.XXXXXX");

	if (d_flag ? mkdtemp(tmp) == NULL : mkstemp(tmp) == -1)
		if (toys.optflags & FLAG_q)
			perror_exit("Failed to create temporary %s",
				d_flag ? "directory" : "file");

	xputs(tmp);

	if (CFG_TOYBOX_FREE && TT.tmpdir) free(tmp);
}
