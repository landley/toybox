/* vi: set sw=4 ts=4:
 *
 * mknod.c - make block or character special file
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * Not in SUSv3.

USE_MKNOD(NEWTOY(mknod, "<2>4", TOYFLAG_BIN))

config MKNOD
	bool "mknod"
	default y
	help
	  usage: mknod NAME TYPE [MAJOR MINOR]

	  Create a special file NAME with a given type, possible types are
	  b       create a block device with the given MAJOR/MINOR
	  c or u  create a character device with the given MAJOR/MINOR
	  p       create a named pipe ignoring MAJOR/MINOR
*/

#include "toys.h"

static const char modes_char[] = {'p', 'c', 'u', 'b'};
static const mode_t modes[] = {S_IFIFO, S_IFCHR, S_IFCHR, S_IFBLK};

void mknod_main(void)
{
	int major=0, minor=0, type;
	char * tmp;
	int mode = 0660;

	tmp = strchr(modes_char, toys.optargs[1][0]);
	if (!tmp)
		perror_exit("unknown special device type %c", toys.optargs[1][0]);

	type = modes[tmp-modes_char];

	if (type == S_IFCHR || type == S_IFBLK) {
                if (toys.optc != 4)
                    perror_exit("creating a block/char device requires major/minor");

		major = atoi(toys.optargs[2]);
		minor = atoi(toys.optargs[3]);
	}

	if (mknod(toys.optargs[0], mode | type, makedev(major, minor)))
		perror_exit("mknod %s failed", toys.optargs[0]);

}
