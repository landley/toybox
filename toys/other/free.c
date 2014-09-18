/* free.c - Display amount of free and used memory in the system.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_FREE(NEWTOY(free, "tgmkb[!tgmkb]", TOYFLAG_USR|TOYFLAG_BIN))

config FREE
  bool "free"
  default y
  help
    usage: free [-bkmgt]

    Display the total, free and used amount of physical memory and swap space.

    -bkmgt	Output units (default is bytes)
*/

#define FOR_free
#include "toys.h"

GLOBALS(
  unsigned bits;
  unsigned long long units;
)

static unsigned long long convert(unsigned long d)
{
  return (d*TT.units)>>TT.bits;
}

void free_main(void)
{
  struct sysinfo in;

  sysinfo(&in);
  TT.units = in.mem_unit ? in.mem_unit : 1;
  for (TT.bits = 0; toys.optflags && !(toys.optflags&(1<<TT.bits)); TT.bits++);
  TT.bits *= 10;

  xprintf("\t\ttotal        used        free      shared     buffers\n"
    "Mem:%17llu%12llu%12llu%12llu%12llu\n-/+ buffers/cache:%15llu%12llu\n"
    "Swap:%16llu%12llu%12llu\n", convert(in.totalram),
    convert(in.totalram-in.freeram), convert(in.freeram), convert(in.sharedram),
    convert(in.bufferram), convert(in.totalram - in.freeram - in.bufferram),
    convert(in.freeram + in.bufferram), convert(in.totalswap),
    convert(in.totalswap - in.freeswap), convert(in.freeswap));
}
