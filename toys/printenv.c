/* vi: set sw=4 ts=4:
 *
 * printenv.c - Print environment variables.
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *

USE_PRINTENV(NEWTOY(printenv, "0", TOYFLAG_USR|TOYFLAG_BIN))

config PRINTENV
	bool "printenv"
	default y
	help
	  usage: printenv [-0] [env_var...]
	  Print enviroment variables.

	  -0	Use \0 as environment delimiter instead of \n
*/

#include "toys.h"

extern char **environ;

void printenv_main(void)
{
	char **env;
	char delim = '\n';

	if (toys.optflags)
		delim = '\0';

	if (!toys.optargs[0]) {
		for (env = environ; *env; env++)
			xprintf("%s%c", *env, delim);
	} else {
		char **var = toys.optargs;
		for (var = toys.optargs; *var; var++) {
			int len = strlen(*var);
			for (env = environ; *env; env++) {
				if (strncmp(*env, *var, len) == 0)
					xprintf("%s%c", *env + len + 1, delim);
			}
		}
	}
}
