/* vi: set sw=4 ts=4:
 *
 * clear.c - clear the screen
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * Not in SUSv4.

USE_CLEAR(NEWTOY(clear, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config CLEAR
	bool "clear"
	default y
	help
	  Clear the screen.
*/

#include "toys.h"

void clear_main(void)
{
	write(1, "\e[2J\e[H", 7);
}
