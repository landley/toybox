/* vi: set sw=4 ts=4:
 *
 * chgrp.c - Change group ownership
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://pubs.opengroup.org/onlinepubs/009695399/utilities/chgrp.html
 *
 * TODO: Add support for -h
 * TODO: Add support for -H
 * TODO: Add support for -L
 * TODO: Add support for -P

USE_CHGRP(NEWTOY(chgrp, "<2Rfv", TOYFLAG_BIN))

config CHGRP
	bool "chgrp"
	default y
	help
	  usage: chgrp [-R] [-f] [-v] group file...
	  Change group ownership of one or more files.

	  -R	recurse into subdirectories.
	  -f	suppress most error messages.
	  -v	verbose output.
*/

#include "toys.h"

#define FLAG_R 4
#define FLAG_f 2
#define FLAG_v 1

DEFINE_GLOBALS(
	gid_t group;
	char *group_name;
)

#define TT this.chgrp

static int do_chgrp(const char *path) {
	int ret = chown(path, -1, TT.group);
	if (toys.optflags & FLAG_v)
		xprintf("chgrp(%s, %s)\n", TT.group_name, path);
	if (ret == -1 && !(toys.optflags & FLAG_f))
		perror_msg("changing group of '%s' to '%s'", path, TT.group_name);
	toys.exitval |= ret;
	return ret;
}

// Copied from toys/cp.c:cp_node()
int chgrp_node(char *path, struct dirtree *node)
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

	do_chgrp(s);

	return 0;
}

void chgrp_main(void)
{
	char **s;
	struct group *group;

	TT.group_name = *toys.optargs;
	group = getgrnam(TT.group_name);
	if (!group) {
		error_msg("invalid group '%s'", TT.group_name);
		toys.exitval = 1;
		return;
	}
	TT.group = group->gr_gid;

	if (toys.optflags & FLAG_R) {
		// Recurse into subdirectories
		for (s=toys.optargs + 1; *s; s++) {
			struct stat sb;
			if (stat(*s, &sb) == -1) {
				if (!(toys.optflags & FLAG_f))
					perror_msg("stat '%s'", *s);
				continue;
			}
			do_chgrp(*s);
			if (S_ISDIR(sb.st_mode)) {
				strncpy(toybuf, *s, sizeof(toybuf) - 1);
				toybuf[sizeof(toybuf) - 1] = 0;
				dirtree_read(toybuf, NULL, chgrp_node);
			}
		}
	} else {
		// Do not recurse
		for (s=toys.optargs + 1; *s; s++) {
			do_chgrp(*s);
		}
	}
}
