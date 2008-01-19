/* vi: set sw=4 ts=4:
 *
 * bzcat.c - decompress stdin to stdout using bunzip2.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.

config BZCAT
	bool "bzcat"
	default n
	help
	  usage: bzcat [filename...]

	  Decompress listed files to stdout.  Use stdin if no files listed.
*/

#include "toys.h"

void bzcat_main(void)
{
	bunzipStream(0, 1);
}
