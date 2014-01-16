/* pwdx.c - report current directory of a process. 
 *
 * Copyright 2013 Lukasz Skalski <l.skalski@partner.samsung.com>

USE_PWDX(NEWTOY(pwdx, "<1a", TOYFLAG_USR|TOYFLAG_BIN))

config PWDX
  bool "pwdx"
  default y
  help
    usage: pwdx PID...

    Print working directory of processes listed on command line.
*/

#include "toys.h"

void pwdx_main(void)
{
  char **optargs;

  for (optargs = toys.optargs; *optargs; optargs++) {
    char *path;
    int num_bytes;

    path = xmprintf("/proc/%s/cwd", *optargs);
    num_bytes = readlink(path, toybuf, sizeof(toybuf)-1);
    free(path);

    if (num_bytes==-1) {
      path = strerror(errno);
      toys.exitval = 1;
    } else {
      path = toybuf;
      toybuf[num_bytes] = 0;
    }
    xprintf("%s: %s\n", *optargs, path);
  }
}
