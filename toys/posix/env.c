/* vi: set sw=4 ts=4:
 *
 * env.c - Set the environment for command invocation.
 *
 * Copyright 2012 Tryn Mirell <tryn@mirell.org>
 *
 * http://opengroup.org/onlinepubs/9699919799/utilities/env.html

USE_ENV(NEWTOY(env, "^i", TOYFLAG_USR|TOYFLAG_BIN))

config ENV
	bool "env"
	default y
	help
          usage: env [-i] [NAME=VALUE...] [command [option...]]

          Set the environment for command invocation.

	  -i	Clear existing environment.
*/

#include "toys.h"

extern char **environ;

void env_main(void)
{
    char **ev;
    char **command = NULL;
    char *del = "=";
    
    if (toys.optflags) clearenv();
    
    for (ev = toys.optargs; *ev != NULL; ev++) {
        char *env, *val = NULL;
        
        env = strtok(*ev, del);
        
        if (env) val = strtok(NULL, del);
        
        if (val) setenv(env, val, 1);
        else {
            command = ev;
            break;
        }
    }
    
    if (!command) {
        char **ep;
        for (ep = environ; *ep; ep++) xputs(*ep);
        return;
    } else xexec(command);
}
