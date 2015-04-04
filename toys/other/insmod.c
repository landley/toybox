/* insmod.c - Load a module into the Linux kernel.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_INSMOD(NEWTOY(insmod, "<1", TOYFLAG_SBIN|TOYFLAG_NEEDROOT))

config INSMOD
  bool "insmod"
  default y
  help
    usage: insmod MODULE [MODULE_OPTIONS]

    Load the module named MODULE passing options if given.
*/

#include "toys.h"

#include <sys/syscall.h>
#define init_module(mod, len, opts) syscall(__NR_init_module, mod, len, opts)

void insmod_main(void)
{
  char * buf = NULL;
  int len, res, i;
  int fd = xopen(*toys.optargs, O_RDONLY);

  len = fdlength(fd);
  buf = xmalloc(len);
  xreadall(fd, buf, len);

  i = 1;
  while(toys.optargs[i] &&
    strlen(toybuf) + strlen(toys.optargs[i]) + 2 < sizeof(toybuf))
  {
    strcat(toybuf, toys.optargs[i++]);
    strcat(toybuf, " ");
  }

  res = init_module(buf, len, toybuf);
  if (CFG_TOYBOX_FREE) {
    if (buf != toybuf) free(buf);
    close(fd);
  }

  if (res) perror_exit("failed to load %s", toys.optargs[0]);
}
