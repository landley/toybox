/* vi: set sw=4 ts=4: */
/*
 * false.c - Return nonzero.
 */

#include "toys.h"

void false_main(void)
{
	toys.exitval = 1;
}
