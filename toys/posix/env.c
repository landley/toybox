/* env.c - Set the environment for command invocation.
 *
 * Copyright 2012 Tryn Mirell <tryn@mirell.org>
 *
 * http://opengroup.org/onlinepubs/9699919799/utilities/env.html
 *
 * Deviations from posix: "-" argument and -0

USE_ENV(NEWTOY(env, "^0iu*", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_ARGFAIL(125)))

config ENV
  bool "env"
  default y
  help
    usage: env [-i] [-u NAME] [NAME=VALUE...] [COMMAND...]

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

void env_main(void)
{
  char **ev = toys.optargs;
  struct arg_list *u;

  // If first nonoption argument is "-" treat it as -i
  if (*ev && **ev == '-' && !(*ev)[1]) {
    toys.optflags |= FLAG_i;
    ev++;
  }

  if (FLAG(i)) xclearenv();
  else for (u = TT.u; u; u = u->next) xunsetenv(u->arg);

  for (; *ev; ev++)
    if (strchr(*ev, '=')) xsetenv(xstrdup(*ev), 0);
    else {
      // a common use of env is to bypass shell builtins
      toys.stacktop = 0;
      xexec(ev);
    }

  for (ev = environ; *ev; ev++) xprintf("%s%c", *ev, '\n'*!FLAG(0));
}
