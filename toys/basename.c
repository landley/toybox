/* vi: set sw=4 ts=4:
 *
 * basename.c  

USE_BASENAME(NEWTOY(basename, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config BASENAME
	bool "basename"
	default n
	help
        usage: basename string [suffix]
        Return non-directory portion of a pathname
*/

#include "toys.h"

void basename_main(void)
{
    char *arg, *suffix, *base;
    int arglen;

    arg = toys.optargs[0];
    suffix = toys.optargs[1];

    // return null string if nothing provided
    if (!arg) return; 

    arglen = strlen(arg);
 
    // handle the case where we only have single slash
    if (arglen == 1 && arg[0] == '/') {
        puts("/");
        return;
    }

    // remove trailing slash
    if (arg[arglen - 1] == '/') {
        arg[arglen - 1] = 0;
    }

    // get everything past the last /
    base = strrchr(arg, '/');

    if (!base) base = arg;
    else base++;
   
    // handle the case where we have all slashes
    if (base[0] == 0) base = "/";  
    
    // chop off the suffix if provided
    if (suffix) {
        strstr(base, suffix)[0] = 0;
    }

    puts(base);
}
