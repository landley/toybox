/* vi: set sw=4 ts=4: */
/*
 * bzcat.c - decompress stdin to stdout using bunzip2.
 */

#include "toys.h"

void bzcat_main(void)
{
	bunzipStream(0, 1);
}
