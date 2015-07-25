/* hostid.c - Print the numeric identifier for the current host.
 *
 * Copyright 2015 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * No Standard.

USE_HOSTID(NEWTOY(hostid, ">0", TOYFLAG_USR|TOYFLAG_BIN))

config HOSTID
  bool "hostid"
  default y
  help
    usage: hostid

    Print the numeric identifier for the current host.
*/
#define FOR_hostid
#include "toys.h"

void hostid_main(void)
{
  xprintf("%08lx\n", gethostid());
}
