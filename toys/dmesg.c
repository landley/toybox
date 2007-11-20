/* vi: set sw=4 ts=4: */
/*
 * dmesg.c - display/control kernel ring buffer.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"
#include <sys/klog.h>

#define TT toy.dmesg

int dmesg_main(void)
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
			else putchar(last = data[i++]);
		}
		if (last!='\n') putchar('\n');
		if (CFG_TOYBOX_FREE) free(data);
	}

	return 0;
}
