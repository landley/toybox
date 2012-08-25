/* vi: set sw=4 ts=4:
 *
 * hostname.c - Get/Set the hostname
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/hostname.html

USE_HOSTNAME(NEWTOY(hostname, NULL, TOYFLAG_BIN))

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
            if (sethostname(hostname, strlen(hostname)))
                perror_exit("cannot set hostname to '%s'", hostname);
        } else {
            if (gethostname(toybuf, sizeof(toybuf)))
                perror_exit("cannot get hostname");
            xputs(toybuf);
        }
}
