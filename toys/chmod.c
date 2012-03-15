/* vi: set sw=4 ts=4:
 *
 * chmod.c - Change file mode bits
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://pubs.opengroup.org/onlinepubs/009695399/utilities/chmod.html
 *

USE_CHMOD(NEWTOY(chmod, "<2Rfv", TOYFLAG_BIN))

config CHMOD
	bool "chmod"
	default n
	help
	  usage: chmod [-R] [-f] [-v] mode file...
	  Change mode bits of one or more files.

	  -R	recurse into subdirectories.
	  -f	suppress most error messages.
	  -v	verbose output.
*/

#include "toys.h"

#define FLAG_R 4
#define FLAG_f 2
#define FLAG_v 1

DEFINE_GLOBALS(
	long mode;
)

#define TT this.chmod

static int do_chmod(const char *path) {
	int ret = chmod(path, TT.mode);
	if (toys.optflags & FLAG_v)
		xprintf("chmod(%04o, %s)\n", TT.mode, path);
	if (ret == -1 && !(toys.optflags & FLAG_f))
		perror_msg("changing perms of '%s' to %04o", path, TT.mode);
	toys.exitval |= ret;
	return ret;
}

// Copied from toys/cp.c:cp_node()
int chmod_node(char *path, struct dirtree *node)
{
	char *s = path + strlen(path);
	struct dirtree *n = node;

	for ( ; ; n = n->parent) {
		while (s!=path) {
			if (*(--s) == '/') break;
		}
		if (!n) break;
	}
	if (s != path) s++;

	do_chmod(s);

	return 0;
}

void chmod_main(void)
{
	char **s;
	TT.mode = strtoul(*toys.optargs, NULL, 8);

	if (toys.optflags & FLAG_R) {
		// Recurse into subdirectories
		for (s=toys.optargs + 1; *s; s++) {
			struct stat sb;
			if (stat(*s, &sb) == -1) {
				if (!(toys.optflags & FLAG_f))
					perror_msg("%s", *s);
				continue;
			}
			do_chmod(*s);
			if (S_ISDIR(sb.st_mode)) {
				strncpy(toybuf, *s, sizeof(toybuf) - 1);
				toybuf[sizeof(toybuf) - 1] = 0;
				dirtree_read(toybuf, NULL, chmod_node);
			}
		}
	} else {
		// Do not recurse
		for (s=toys.optargs + 1; *s; s++) {
			do_chmod(*s);
		}
	}
}
