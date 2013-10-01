/* reboot.c - Restart, halt or powerdown the system.
 *
 * Copyright 2013 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_REBOOT(NEWTOY(reboot, "n", TOYFLAG_BIN|TOYFLAG_NEEDROOT))
USE_REBOOT(OLDTOY(halt, reboot, "n", TOYFLAG_BIN|TOYFLAG_NEEDROOT))
USE_REBOOT(OLDTOY(poweroff, reboot, "n", TOYFLAG_BIN|TOYFLAG_NEEDROOT))

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
  char c = toys.which->name[0];

  if (!(toys.optflags & FLAG_n))
    sync();

  switch(c) {
  case 'p':
    toys.exitval = reboot(RB_POWER_OFF);
      break;
  case 'h':
    toys.exitval = reboot(RB_HALT_SYSTEM);
    break;
  case 'r':
  default:
    toys.exitval = reboot(RB_AUTOBOOT);
  }
}
