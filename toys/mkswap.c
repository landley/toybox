/* vi: set sw=4 ts=4:
 *
 * mkswap.c - Format swap device.
 *
 * Copyright 2009 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.

USE_MKSWAP(NEWTOY(mkswap, "<1>2", TOYFLAG_SBIN))

config MKSWAP
	bool "mkswap"
	default y
	help
	  usage: mkswap DEVICE

	  Format a Linux v1 swap device.
*/

#include "toys.h"

void mkswap_main(void)
{
	int fd = xopen(*toys.optargs, O_RDWR), pagesize = getpagesize();
	off_t len = fdlength(fd);
	unsigned int pages = (len/pagesize)-1, *swap = (unsigned int *)toybuf;

	// Write header.  Note that older kernel versions checked signature
	// on disk (not in cache) during swapon, so sync after writing.

	swap[0] = 1;
	swap[1] = pages;
	xlseek(fd, 1024, SEEK_SET);
	xwrite(fd, swap, 129*sizeof(unsigned int));
	xlseek(fd, pagesize-10, SEEK_SET);
	xwrite(fd, "SWAPSPACE2", 10);
	fsync(fd);

	if (CFG_TOYBOX_FREE) close(fd);

	printf("Swapspace size: %luk\n", pages*(unsigned long)(pagesize/1024));
}
