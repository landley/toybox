/* vi: set sw=4 ts=4:
 * env.c

USE_ENV(NEWTOY(env, "^i", TOYFLAG_USR|TOYFLAG_BIN))

config ENV
	bool "env"
	default n
	help
        usage: env [-i] [FOO=BAR...] [command [option...]]

        Set the environment for command invocation
*/

#include "toys.h"

extern char **environ;

void env_main(void)
{
    char **ev;
    char **command = NULL;
    char *del = "=";
    
    if (toys.optflags & 1) clearenv();
    
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
        for (ep = environ; *ep; ep++)
            xputs(*ep);
        return;
    } else execvp(*command, command);
}
