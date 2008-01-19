/* vi: set sw=4 ts=4:
 *
 * true.c - Return zero.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/true.html

config TRUE
	bool "true"
	default y
	help
	  Return zero.
*/

#include "toys.h"

void true_main(void)
{
	return;
}
