/* chvt.c - switch virtual terminals
 *
 * Copyright (C) 2008 David Anders <danders@amltd.com>

USE_CHVT(NEWTOY(chvt, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config CHVT
  bool "chvt"
  default y
  help
    usage: chvt N

    Change to virtual terminal number N. (This only works in text mode.)

    Virtual terminals are the Linux VGA text mode displays, ordinarily
    switched between via alt-F1, alt-F2, etc. Use ctrl-alt-F1 to switch
    from X to a virtual terminal, and alt-F6 (or F7, or F8) to get back.
*/

#include "toys.h"
#include <linux/vt.h>

void chvt_main(void)
{
  int vt, fd;
  char *consoles[]={"/dev/console", "/dev/vc/0", "/dev/tty", NULL}, **cc;

  vt = atoi(*toys.optargs);
  for (cc = consoles; *cc; cc++) if ((fd = open(*cc, O_RDWR)) != -1) break;

  if (fd == -1 || ioctl(fd, VT_ACTIVATE, vt) || ioctl(fd, VT_WAITACTIVE, vt))
    perror_exit(0);
}
