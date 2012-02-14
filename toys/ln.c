/* vi: set sw=4 ts=4:
 *
 * ln.c - Create filesystem links
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ln.html

USE_LN(NEWTOY(ln, "fs", TOYFLAG_BIN))

config LN
	bool "ln"
	default n
	help
	  usage: ln [-s] [-f] file1 file2

          Create a link from file2 to file1

          -s    Create a symbolic link
          -f    Force the creation of the link, even if file2 already exists
*/

#include "toys.h"

#define FLAG_s	1
#define FLAG_f  2

void ln_main(void)
{
    char *file1 = toys.optargs[0], *file2 = toys.optargs[1];

    /* FIXME: How do we print out the usage info? */
    if (!file1 || !file2)
        perror_exit("Usage: ln [-s] [-f] file1 file2");
    /* Silently unlink the existing target. If it doesn't exist,
     * then we just move on */
    if (toys.optflags & FLAG_f)
        unlink(file2);
    if (toys.optflags & FLAG_s) {
        if (symlink(file1, file2))
            perror_exit("cannot create symbolic link from %s to %s",
                    file1, file2);
    } else {
        if (link(file1, file2))
            perror_exit("cannot create hard link from %s to %s",
                    file1, file2);
    }
}
