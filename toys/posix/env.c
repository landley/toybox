/* env.c - Set the environment for command invocation.
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

  if (toys.optflags) clearenv();

  for (ev = toys.optargs; *ev; ev++) {
    char *name = *ev, *val = strchr(name, '=');

    if (val) {
      *(val++) = 0;
      if (*val) setenv(name, val, 1);
      else unsetenv(name);
    } else xexec(ev);
  }

  if (environ) for (ev = environ; *ev; ev++) xputs(*ev);
}
