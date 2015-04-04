/* renice.c - renice process
 *
 * Copyright 2013 CE Strake <strake888 at gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/renice.html

USE_RENICE(NEWTOY(renice, "<1gpun#|", TOYFLAG_USR|TOYFLAG_BIN))

config RENICE
  bool "renice"
  default y
  help
    usage: renice [-gpu] -n increment ID ...
*/

#define FOR_renice
#include "toys.h"

GLOBALS(
  long nArgu;
)

void renice_main(void) {
  int which = (toys.optflags & FLAG_g) ? PRIO_PGRP :
              ((toys.optflags & FLAG_u) ? PRIO_USER : PRIO_PROCESS);
  char **arg;

  for (arg = toys.optargs; *arg; arg++) {
    char *s = *arg;
    int id = -1;

    if (toys.optflags & FLAG_u) {
      struct passwd *p = getpwnam(s);
      if (p) id = p->pw_uid;
    } else {
      id = strtol(s, &s, 10);
      if (*s) id = -1;
    }

    if (id < 0) {
      error_msg("bad '%s'", *arg);
      continue;
    }

    if (setpriority(which, id, getpriority(which, id)+TT.nArgu) < 0)
      perror_msg("setpriority %d", id);
  }
}
