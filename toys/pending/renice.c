/* renice.c - renice process
 *
 * Copyright 2013 CE Strake <strake888 at gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cmdbehav.html

USE_RENICE(NEWTOY(renice, "gpun#", TOYFLAG_BIN))

config RENICE
  bool "renice"
  default n
  help
    usage: renice [-gpu] -n increment ID ...
*/

#define FOR_renice
#include "toys.h"

GLOBALS(
  long nArgu;
)

void renice_main (void) {
  int ii;
  int which = toys.optflags & FLAG_g ? PRIO_PGRP :
              toys.optflags & FLAG_u ? PRIO_USER :
              PRIO_PROCESS;

  if (!(toys.optflags & FLAG_n)) error_exit ("no increment given");

  for (ii = 0; ii < toys.optc; ii++) {
    id_t id;

    if (isdigit (toys.optargs[ii][0])) id = strtoul (toys.optargs[ii], 0, 10);
    else if (toys.optflags & FLAG_u) id = getpwnam (toys.optargs[ii]) -> pw_uid;
    else {
      error_msg ("not a number: %s", toys.optargs[ii]);
      toys.exitval = 1;
      continue;
    }

    if (setpriority (which, id, getpriority (which, id) + TT.nArgu) < 0) {
      error_msg ("failed to setpriority of %d", id);
      toys.exitval = 1;
    }
  }
}
