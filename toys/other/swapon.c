/* swapon.c - Enable region for swapping
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_SWAPON(NEWTOY(swapon, "<1>1p#<0>32767", TOYFLAG_SBIN|TOYFLAG_NEEDROOT))

config SWAPON
  bool "swapon"
  default y
  help
    usage: swapon [-p priority] filename

    Enable swapping on a given device/file.
*/

#define FOR_swapon
#include "toys.h"

GLOBALS(
  long priority;
)

void swapon_main(void)
{
  int flags = 0;

  if (toys.optflags)
    flags = SWAP_FLAG_PREFER | (TT.priority << SWAP_FLAG_PRIO_SHIFT);

  if (swapon(*toys.optargs, flags))
    perror_exit("Couldn't swapon '%s'", *toys.optargs);
}
