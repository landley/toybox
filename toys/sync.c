/* vi: set sw=4 ts=4: */
/*
 * sync.c - Write all pending data to disk.
 */

#include "toys.h"

int sync_main(void)
{
	sync();
	return 0;
}
