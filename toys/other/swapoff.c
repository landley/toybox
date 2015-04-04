/* swapoff.c - Disable region for swapping
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_SWAPOFF(NEWTOY(swapoff, "<1>1", TOYFLAG_SBIN|TOYFLAG_NEEDROOT))

config SWAPOFF
  bool "swapoff"
  default y
  help
    usage: swapoff swapregion

    Disable swapping on a given swapregion.
*/

#include "toys.h"

void swapoff_main(void)
{
  if (swapoff(toys.optargs[0])) perror_exit("failed to remove swaparea");
}
