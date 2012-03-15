/* vi: set sw=4 ts=4:
 *
 * chown.c - Change ownership
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://pubs.opengroup.org/onlinepubs/009695399/utilities/chown.html
 *
 * TODO: Add support for -h
 * TODO: Add support for -H
 * TODO: Add support for -L
 * TODO: Add support for -P

USE_CHOWN(NEWTOY(chown, "<2Rfv", TOYFLAG_BIN))

config CHOWN
	bool "chown"
	default n
	help
	  usage: chown [-R] [-f] [-v] group file...
	  Change ownership of one or more files.

	  -R	recurse into subdirectories.
	  -f	suppress most error messages.
	  -v	verbose output.
*/

#include "toys.h"

#define FLAG_R 4
#define FLAG_f 2
#define FLAG_v 1

DEFINE_GLOBALS(
	uid_t owner;
	gid_t group;
	char *owner_name;
	char *group_name;
)

#define TT this.chown

static int do_chown(const char *path) {
	int ret = chown(path, TT.owner, TT.group);
	if (toys.optflags & FLAG_v)
		xprintf("chown(%s:%s, %s)\n", TT.owner_name, TT.group_name, path);
	if (ret == -1 && !(toys.optflags & FLAG_f))
		perror_msg("changing owner of '%s' to '%s:%s'", path,
			TT.owner_name, TT.group_name);
	toys.exitval |= ret;
	return ret;
}

// Copied from toys/cp.c:cp_node()
int chown_node(char *path, struct dirtree *node)
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

	do_chown(s);

	return 0;
}

void chown_main(void)
{
	char **s;
	char *owner = NULL, *group;
	char *param1 = *toys.optargs;

	TT.owner = -1;
	TT.group = -1;
	TT.owner_name = "";
	TT.group_name = "";

	group = strchr(param1, ':');
	if (!group)
		group = strchr(param1, '.');

	if (group) {
		group++;
		struct group *g = getgrnam(group);
		if (!g) {
			error_msg("invalid group '%s'", group);
			toys.exitval = 1;
			return;
		}
		TT.group = g->gr_gid;
		TT.group_name = group;
		owner = param1;
		owner[group - owner - 1] = '\0';
	} else {
		owner = param1;
	}

	if (owner && owner[0]) {
		struct passwd *p = getpwnam(owner);
		if (!p) {
			error_msg("invalid owner '%s'", owner);
			toys.exitval = 1;
			return;
		}
		TT.owner = p->pw_uid;
		TT.owner_name = owner;
	}

	if (toys.optflags & FLAG_R) {
		// Recurse into subdirectories
		for (s=toys.optargs + 1; *s; s++) {
			struct stat sb;
			if (stat(*s, &sb) == -1) {
				if (!(toys.optflags & FLAG_f))
					perror_msg("%s", *s);
				continue;
			}
			do_chown(*s);
			if (S_ISDIR(sb.st_mode)) {
				strncpy(toybuf, *s, sizeof(toybuf) - 1);
				toybuf[sizeof(toybuf) - 1] = 0;
				dirtree_read(toybuf, NULL, chown_node);
			}
		}
	} else {
		// Do not recurse
		for (s=toys.optargs + 1; *s; s++) {
			do_chown(*s);
		}
	}
}
