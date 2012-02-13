/* vi: set sw=4 ts=4:
 *
 * free.c - Display amount of free and used memory in the system.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * Not in SUSv3.

USE_FREE(NEWTOY(free, "gmkb", TOYFLAG_USR|TOYFLAG_BIN))

config FREE
	bool "free"
	default y
	help
	  usage: free [-bkmg]

	  Display the total, free and used amount of physical memory and
	  swap space.
	  -bkmg    Output in bytes (default), KB, MB or GB
*/

#include "toys.h"
#include <sys/sysinfo.h>


static unsigned long long convert(unsigned long d, unsigned int iscale,
				unsigned int oscale)
{
	return ((unsigned long long)d*iscale)>>oscale;
}

void free_main(void)
{
	struct sysinfo info;
	unsigned int iscale = 1;
	unsigned int oscale = 0;

	sysinfo(&info);
	if (info.mem_unit) iscale = info.mem_unit;
	if (toys.optflags & 1) oscale = 0;
	if (toys.optflags & 2) oscale = 10;
	if (toys.optflags & 4) oscale = 20;
	if (toys.optflags & 8) oscale = 30;

	printf("              total        used        free      shared     buffers\n");
	printf("Mem:   %12llu%12llu%12llu%12llu%12llu\n",
		convert(info.totalram, iscale, oscale),
		convert(info.totalram-info.freeram, iscale, oscale),
		convert(info.freeram, iscale, oscale),
		convert(info.sharedram, iscale, oscale),
		convert(info.bufferram, iscale, oscale));

	printf("-/+ buffers/cache: %12llu%12llu\n",
		convert(info.totalram - info.freeram - info.bufferram, iscale, oscale),
		convert(info.freeram + info.bufferram, iscale, oscale));

	printf("Swap:  %12llu%12llu%12llu\n",
		convert(info.totalswap, iscale, oscale),
		convert(info.totalswap - info.freeswap, iscale, oscale),
		convert(info.freeswap, iscale, oscale));
}
