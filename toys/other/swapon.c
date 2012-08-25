/* vi: set sw=4 ts=4:
 *
 * swapon.c - Enable region for swapping
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_SWAPON(NEWTOY(swapon, "<1>1p#<0>32767", TOYFLAG_BIN|TOYFLAG_NEEDROOT))

config SWAPON
	bool "swapon"
	default y
	help
	  usage: swapon [-p priority] filename

	  Enable swapping on a given device/file.
*/

#include "toys.h"

DEFINE_GLOBALS(
	long priority;
)

#define TT this.swapon

void swapon_main(void)
{
	int flags = 0;

	if (toys.optflags & 1)
		flags = SWAP_FLAG_PREFER | (TT.priority << SWAP_FLAG_PRIO_SHIFT);

	if (swapon(*toys.optargs, flags))
		perror_exit("Couldn't swapon '%s'", *toys.optargs);
}
