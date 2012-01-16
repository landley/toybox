/* vi: set sw=4 ts=4:
 * env.c

USE_ENV(NEWTOY(env, "^?i", TOYFLAG_USR|TOYFLAG_BIN))

config ENV
	bool "env"
	default n
	help
        
        Set the environment for command invocation
*/

#include "toys.h"

extern char **environ;

void env_main(void)
{
    char **ev;
    char **command = NULL;

    if (toys.optflags & 1) clearenv();
    
    for (ev = toys.optargs; *ev != NULL; ev++) {
        char *env = NULL, *val = NULL;
        char *del = "=";
        
        env = strtok(*ev, del);
        
        if (env != NULL) val = strtok(NULL, del);
        
        if (val != NULL) {
            setenv(env, val, 1);
        } else {
            command = ev;
            break;
        }
    }
    
    if (!command) {
        char **ep;
        if (environ) {
            for (ep = environ; *ep != NULL; ep++)
                xputs(*ep);
            return;
        }
    } else {
        execvp(*command, command);
    }

}
