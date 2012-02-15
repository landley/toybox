/* vi: set sw=4 ts=4:
 *
 * hostname.c - Get/Set the hostname
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * Not in SUSv4.

USE_HOSTNAME(NEWTOY(hostname, "", TOYFLAG_BIN))

config HOSTNAME
	bool "hostname"
	default n
	help
	  usage: hostname [newname]

	  Get/Set the current hostname
*/

#include "toys.h"

void hostname_main(void)
{
	const char *hostname = toys.optargs[0];
        if (hostname) {
            int len = strlen(hostname);
            if (sethostname(hostname, len))
                perror_exit("cannot set hostname to '%s'", hostname);
        } else {
            char buffer[256];
            if (gethostname(buffer, sizeof(buffer)))
                perror_exit("cannot get hostname");
            xprintf("%s\n", buffer);
        }
}
