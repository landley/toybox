/* vi: set sw=4 ts=4: */
/*
 * count.c - Progress indicator from stdin to stdout
 */

#include "toys.h"

int count_main(void)
{
	uint64_t size = 0;
	int len;

	for (;;) {
		len = xread(0, toybuf, sizeof(toybuf));
		if (!len) break;
		size += len;
		xwrite(1, toybuf, sizeof(toybuf));
		fdprintf(2, "%"PRIu64" bytes\r", size);
	}
	fdprintf(2,"\n");
	return 0;
}
