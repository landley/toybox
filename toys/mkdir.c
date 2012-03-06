/* vi: set sw=4 ts=4:
 *
 * mkdir.c - Make directories
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://pubs.opengroup.org/onlinepubs/009695399/utilities/mkdir.html
 *
 * TODO: Add -m

USE_MKDIR(NEWTOY(mkdir, "<1p", TOYFLAG_BIN))

config MKDIR
	bool "mkdir"
	default y
	help
	  usage: mkdir [-p] [dirname...]
	  Create one or more directories.

	  -p	make parent directories as needed.
*/

#include "toys.h"


static int create_dir(const char *dir, mode_t mode) {
	int ret = 0;
	unsigned int i;

	// Shortcut
	if (strchr(dir, '/') == NULL || !toys.optflags)
		return mkdir(dir, mode);

	char *d = strdup(dir);
	if (!d)
		return -1;
	unsigned int dlen = strlen(dir);

	// Skip first char (it can be /)
	for (i = 1; i < dlen; i++) {
		if (d[i] != '/')
			continue;
		d[i] = '\0';
		ret = mkdir(d, mode);
		d[i] = '/';
		if (ret < 0 && errno != EEXIST)
			goto OUT;
	}
	ret = mkdir(d, mode);
OUT:
	free(d);
	return ret;
}

void mkdir_main(void)
{
	char **s;
	mode_t umask_val = umask(0);
	mode_t dir_mode = (0777 & ~umask_val) | (S_IWUSR | S_IXUSR);
	umask(umask_val);

	for (s=toys.optargs; *s; s++) {
		if (create_dir(*s, dir_mode) != 0) {
			fprintf(stderr, "mkdir: cannot create directory '%s': %s\n", *s, strerror(errno));
			toys.exitval = 1;
		}
	}
}
