/* env.c - Set the environment for command invocation.
 *
 * Copyright 2012 Tryn Mirell <tryn@mirell.org>
 *
 * http://opengroup.org/onlinepubs/9699919799/utilities/env.html
 *
 * Deviations from posix: "-" argument and -0

USE_ENV(NEWTOY(env, "^0iu*", TOYFLAG_USR|TOYFLAG_BIN))

config ENV
  bool "env"
  default y
  help
    usage: env [-i] [-u NAME] [NAME=VALUE...] [command [option...]]

    Set the environment for command invocation, or list environment variables.

    -i	Clear existing environment
    -u NAME	Remove NAME from the environment
    -0	Use null instead of newline in output
*/

#define FOR_env
#include "toys.h"

GLOBALS(
  struct arg_list *u;
);

extern char **environ;

void env_main(void)
{
  char **ev = toys.optargs;

  // If first nonoption argument is "-" treat it as -i
  if (*ev && **ev == '-' && !(*ev)[1]) {
    toys.optflags |= FLAG_i;
    ev++;
  }

  if (toys.optflags & FLAG_i) clearenv();
  while (TT.u) {
    unsetenv(TT.u->arg);
    TT.u = TT.u->next;
  }

  for (; *ev; ev++) {
    char *name = *ev, *val = strchr(name, '=');

    if (val) {
      *(val++) = 0;
      setenv(name, val, 1);
    } else xexec(ev);
  }

  if (environ) for (ev = environ; *ev; ev++)
    xprintf("%s%c", *ev, '\n'*!(toys.optflags&FLAG_0));
}
