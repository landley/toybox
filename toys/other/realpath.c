/* realpath.c - Return the canonical version of a pathname
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>

USE_REALPATH(NEWTOY(realpath, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config REALPATH
  bool "realpath"
  default y
  help
    usage: realpath FILE...

    Display the canonical absolute pathname
*/

#include "toys.h"

void realpath_main(void)
{
  char **s = toys.optargs;

  for (s = toys.optargs; *s; s++) {
    if (!realpath(*s, toybuf)) perror_msg_raw(*s);
    else xputs(toybuf);
  }
}
