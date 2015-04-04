/* reboot.c - Restart, halt or powerdown the system.
 *
 * Copyright 2013 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_REBOOT(NEWTOY(reboot, "n", TOYFLAG_SBIN|TOYFLAG_NEEDROOT))
USE_REBOOT(OLDTOY(halt, reboot, TOYFLAG_SBIN|TOYFLAG_NEEDROOT))
USE_REBOOT(OLDTOY(poweroff, reboot, TOYFLAG_SBIN|TOYFLAG_NEEDROOT))

config REBOOT
  bool "reboot"
  default y
  help
    usage: reboot/halt/poweroff [-n]

    Restart, halt or powerdown the system.

    -n	Don't sync before stopping the system.
*/

#define FOR_reboot
#include "toys.h"
#include <sys/reboot.h>

void reboot_main(void)
{
  int types[] = {RB_AUTOBOOT, RB_HALT_SYSTEM, RB_POWER_OFF};

  if (!(toys.optflags & FLAG_n)) sync();

  toys.exitval = reboot(types[stridx("hp", *toys.which->name)+1]);
}
