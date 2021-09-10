/* reboot.c - Restart, halt or powerdown the system.
 *
 * Copyright 2013 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_REBOOT(NEWTOY(reboot, "d:fn", TOYFLAG_SBIN|TOYFLAG_NEEDROOT))
USE_REBOOT(OLDTOY(halt, reboot, TOYFLAG_SBIN|TOYFLAG_NEEDROOT))
USE_REBOOT(OLDTOY(poweroff, reboot, TOYFLAG_SBIN|TOYFLAG_NEEDROOT))

config REBOOT
  bool "reboot"
  default y
  help
    usage: reboot/halt/poweroff [-fn] [-d DURATION]

    Restart, halt, or power off the system.

    DURATION can be a decimal fraction. An optional suffix can be "m"
    (minutes), "h" (hours), "d" (days), or "s" (seconds, the default).

    -d	Delay before proceeding
    -f	Don't signal init
    -n	Don't sync before stopping the system
*/

#define FOR_reboot
#include "toys.h"
#include <sys/reboot.h>

GLOBALS(
  char *d;
)

void reboot_main(void)
{
  struct timespec tv;
  int types[] = {RB_AUTOBOOT, RB_HALT_SYSTEM, RB_POWER_OFF},
      sigs[] = {SIGTERM, SIGUSR1, SIGUSR2}, idx;

  if (TT.d) {
    tv.tv_sec = xparsetime(TT.d, 9, &tv.tv_nsec);
    nanosleep(&tv, NULL);
  }

  if (!FLAG(n)) sync();

  idx = stridx("hp", *toys.which->name)+1;
  if (FLAG(f)) toys.exitval = reboot(types[idx]);
  else toys.exitval = kill(1, sigs[idx]);
}
