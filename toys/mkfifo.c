/* vi: set sw=4 ts=4:
 *
 * mkfifo.c: Create a named pipe.
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/mkfifo.html

USE_MKFIFO(NEWTOY(mkfifo, "<1m:", TOYFLAG_BIN))

config MKFIFO
	bool "mkfifo"
	default y
	help
	  usage: mkfifo [-m mode] name...

	  Makes a named pipe at name.

	  -m mode	The mode of the pipe(s) created by mkfifo. It defaults
			to 0644.  This number is in octal, optionally preceded
			by a leading zero.
*/

#include "toys.h"

void mkfifo_main(void)
{
	char *arg;
	int i;
	mode_t mode;

	if (toys.optflags) {
		char *end;
		mode = (mode_t)strtol(toy.mkfifo.mode, &end, 8);
		if (end<=toy.mkfifo.mode || *end || mode<0 || mode>0777)
			error_exit("Invalid mode");
	} else mode = 0644;

	umask(0);
	for (i = 0; (arg = toys.optargs[i]); i++)
		if (mkfifo(arg, mode))
			perror_exit(arg);
}
