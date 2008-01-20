/* vi: set sw=4 ts=4:
 *
 * dmesg.c - display/control kernel ring buffer.
 *
 * Copyright 2006, 2007 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.

USE_DMESG(NEWTOY(dmesg, "s#n#c", TOYFLAG_BIN))

config DMESG
	bool "dmesg"
	default y
	help
	  usage: dmesg [-n level] [-s bufsize] | -c

	  Print or control the kernel ring buffer.

	  -n	Set kernel logging level (1-9).
	  -s	Size of buffer to read (in bytes), default 16384.
	  -c	Clear the ring buffer after printing.
*/

#include "toys.h"
#include <sys/klog.h>

DEFINE_GLOBALS(
	long level;
	long size;
)

#define TT this.dmesg

void dmesg_main(void)
{
	// For -n just tell kernel to which messages to keep.
	if (toys.optflags & 2) {
		if (klogctl(8, NULL, TT.level))
			error_exit("klogctl");
	} else {
		int size, i, last = '\n';
		char *data;

		// Figure out how much data we need, and fetch it.
		size = TT.size;
		if (size<2) size = 16384;
		data = xmalloc(size);
		size = klogctl(3 + (toys.optflags&1), data, size);
		if (size < 0) error_exit("klogctl");

		// Display data, filtering out level markers.
		for (i=0; i<size; ) {
			if (last=='\n' && data[i]=='<') i += 3;
			else xputc(last = data[i++]);
		}
		if (last!='\n') xputc('\n');
		if (CFG_TOYBOX_FREE) free(data);
	}
}
