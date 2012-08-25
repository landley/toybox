/* vi: set sw=4 ts=4:
 *
 * pidof.c - Print the PIDs of all processes with the given names.
 *
 * Copyright 2012 Andreas Heck <aheck@gmx.de>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/pidof.html

USE_PIDOF(NEWTOY(pidof, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config PIDOF
	bool "pidof"
	default y
	help
	  usage: pidof [NAME]...

	  Print the PIDs of all processes with the given names.
*/

#include "toys.h"

static void print_pid(pid_t pid) {
    xprintf("%s%ld", toys.exitval ? "" : " ", (long)pid);
    toys.exitval = 0;
}

void pidof_main(void)
{
    toys.exitval = 1;
    for_each_pid_with_name_in(toys.optargs, print_pid);
    if (!toys.exitval) xputc('\n');
}
