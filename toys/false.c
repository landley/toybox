/* vi: set sw=4 ts=4: */
/*
 * false.c - Return nonzero.
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/false.html
 */

#include "toys.h"

void false_main(void)
{
	toys.exitval = 1;
}
