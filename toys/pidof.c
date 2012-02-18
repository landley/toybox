/* vi: set sw=4 ts=4:
 *
 * pidof.c - Print the PIDs of all processes with the given names.
 *
 * Copyright 2012 Andreas Heck <aheck@gmx.de>
 *
 * Not in SUSv4.
 * See http://opengroup.org/onlinepubs/9699919799/utilities/

USE_PIDOF(NEWTOY(pidof, "", TOYFLAG_USR|TOYFLAG_BIN))

config PIDOF
	bool "pidof"
	default y
	help
	  usage: pidof [NAME]...

	  Print the PIDs of all processes with the given names.
*/

#include "toys.h"

DEFINE_GLOBALS(
		int matched;
)
#define TT this.pidof


static void print_pid (const char *pid) {
    if (TT.matched) putchar(' ');
    fputs(pid, stdout);
    TT.matched = 1;
}

void pidof_main(void)
{
    int err;

	TT.matched = 0;

    if (!toys.optargs) exit(1);

    err = for_each_pid_with_name_in(toys.optargs, print_pid);
    if (err) exit(1);

    if (!TT.matched)
        exit(1);
    else
        putchar('\n');
}
