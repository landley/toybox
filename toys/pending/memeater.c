/* memeater.c - consume the specified amount of memory
 *
 * Copyright 2024 The Android Open Source Project

USE_MEMEATER(NEWTOY(memeater, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config MEMEATER
  bool "memeater"
  default y
  help
    usage: memeater SIZE

    Consume the specified amount of memory (in bytes, with optional suffix).
*/

#define FOR_memeater
#include "toys.h"

void memeater_main(void)
{
  long long size = atolx(*toys.optargs), i;
  char* p;

  p = xmalloc(size);
  // Lock the physical pages.
  if (mlock(p, size)) perror_exit("mlock");
  // And ensure we weren't cheated by overcommit...
  for (i = 0; i < size; i++) p[i] = i;

  while (1) pause();
}
