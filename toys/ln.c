/* vi: set sw=4 ts=4:
 *
 * ln.c - Create filesystem links
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ln.html

USE_LN(NEWTOY(ln, "<1fs", TOYFLAG_BIN))

config LN
	bool "ln"
	default y
	help
	  usage: ln [-sf] [FROM...] TO

          Create a link between FROM and TO.
          With only one argument, create link in current directory.

          -s    Create a symbolic link
          -f    Force the creation of the link, even if TO already exists
*/

#include "toys.h"

#define FLAG_s	1
#define FLAG_f  2

void ln_main(void)
{
    char *dest = toys.optargs[--toys.optc], *new;
    struct stat buf;
    int i;

    // With one argument, create link in current directory.
    if (!toys.optc) {
        toys.optc++;
        dest=".";
    }

    // Is destination a directory?
    if (stat(dest, &buf) || !S_ISDIR(buf.st_mode)) {
        if (toys.optc>1) error_exit("'%s' not a directory");
        buf.st_mode = 0;
    }

    for (i=0; i<toys.optc; i++) {
        int rc;
        char *try = toys.optargs[i];

        if (S_ISDIR(buf.st_mode)) {
            new = strrchr(try, '/');
            if (!new) new = try;
            new = xmsprintf("%s/%s", dest, new);
        } else new = dest;
        /* Silently unlink the existing target. If it doesn't exist,
         * then we just move on */
        if (toys.optflags & FLAG_f) unlink(new);


        rc = (toys.optflags & FLAG_s) ? symlink(try, new) : link(try, new);
        if (rc)
            perror_exit("cannot create %s link from '%s' to '%s'",
                (toys.optflags & FLAG_s) ? "symbolic" : "hard", try, new);
        if (new != dest) free(new);
    }
}
