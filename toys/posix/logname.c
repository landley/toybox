/* vi: set sw=4 ts=4:
 *
 * logname.c - Print user's login name.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/logname.html

USE_LOGNAME(NEWTOY(logname, ">0", TOYFLAG_BIN))

config LOGNAME
	bool "logname"
	default y
	help
	  usage: logname

	  Prints the calling user's name or an error when this cannot be
	  determined.
*/

#include "toys.h"

void logname_main(void)
{
	if (getlogin_r(toybuf, sizeof(toybuf))) error_exit("no login name");
	xputs(toybuf);
}
