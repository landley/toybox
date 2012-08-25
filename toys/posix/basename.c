/* vi: set sw=4 ts=4:
 *
 * basename.c - Return non-directory portion of a pathname
 *
 * Copyright 2012 Tryn Mirell <tryn@mirell.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/basename.html


USE_BASENAME(NEWTOY(basename, "<1>2", TOYFLAG_USR|TOYFLAG_BIN))

config BASENAME
	bool "basename"
	default y
	help
        usage: basename string [suffix]

        Return non-directory portion of a pathname removing suffix
*/

#include "toys.h"

void basename_main(void)
{
    char *arg = toys.optargs[0], *suffix = toys.optargs[1], *base;

    while ((base = strrchr(arg, '/'))) {
        if (base == arg) break;
        if (!base[1]) *base = 0;
        else {
            base++;
            break;
        }
    }

    if (!base) base = arg;
    
    // chop off the suffix if provided
    if (suffix) {
        arg = base + strlen(base) - strlen(suffix);
        if (arg > base && !strcmp(arg, suffix)) *arg = 0;
    }
 
    puts(base);
}
