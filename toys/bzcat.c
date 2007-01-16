/* vi: set sw=4 ts=4: */
/*
 * bzcat.c - decompress stdin to stdout using bunzip2.
 */

#include "toys.h"

int bzcat_main(void)
{
	char *error = bunzipStream(0, 1);

	if (error) error_exit(error);
	return 0;
}
