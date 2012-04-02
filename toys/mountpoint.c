/* vi: set sw=4 ts=4:
 *
 * mountpoint.c - Check if a directory is a mountpoint.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * Not in SUSv4.

USE_MOUNTPOINT(NEWTOY(mountpoint, "<1qdx", TOYFLAG_BIN))

config MOUNTPOINT
	bool "mountpoint"
	default y
	help
	  usage: mountpoint [-q] [-d] directory
			 mountpoint [-q] [-x] device 
	  -q Be quiet, return zero if directory is a mountpoint
	  -d Print major/minor device number of the directory
	  -x Print major/minor device number of the block device
*/

#include "toys.h"

void mountpoint_main(void)
{
	struct stat st1, st2;
	int res = 0;
	int quiet = toys.optflags & 0x4;
	toys.exitval = 1; // be pessimistic
	strncpy(toybuf, toys.optargs[0], sizeof(toybuf));
	if (((toys.optflags & 0x1) && lstat(toybuf, &st1)) || stat(toybuf, &st1))
		perror_exit("%s", toybuf);

	if (toys.optflags & 0x1){
		if (S_ISBLK(st1.st_mode)) {
			if (!quiet) printf("%u:%u\n", major(st1.st_rdev), minor(st1.st_rdev));
			toys.exitval = 0;
			return;
		}
		if (!quiet) printf("%s: not a block device\n", toybuf);
		return;
	}

	if(!S_ISDIR(st1.st_mode)){
		if (!quiet) printf("%s: not a directory\n", toybuf);
		return;
	}
	strncat(toybuf, "/..", sizeof(toybuf));
	stat(toybuf, &st2);
	res = (st1.st_dev != st2.st_dev) ||
		(st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);
	if (!quiet) printf("%s is %sa mountpoint\n", toys.optargs[0], res ? "" : "not ");
	if (toys.optflags & 0x2)
		printf("%u:%u\n", major(st1.st_dev), minor(st1.st_dev));
	toys.exitval = res ? 0 : 1;
}
