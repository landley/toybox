/* vi: set sw=4 ts=4: */
/*
 * sync.c - Write all pending data to disk.
 */

#include "toys.h"

void sync_main(void)
{
	sync();
}
