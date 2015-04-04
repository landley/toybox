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

void chvt_main(void)
{
  int vtnum, fd = fd;
  char *consoles[]={"/dev/console", "/dev/vc/0", "/dev/tty", NULL}, **cc;

  vtnum=atoi(*toys.optargs);
  for (cc = consoles; *cc; cc++)
    if (-1 != (fd = open(*cc, O_RDWR))) break;

  // These numbers are VT_ACTIVATE and VT_WAITACTIVE from linux/vt.h
  if (!*cc || fd < 0 || ioctl(fd, 0x5606, vtnum) || ioctl(fd, 0x5607, vtnum))
    perror_exit(0);
}
