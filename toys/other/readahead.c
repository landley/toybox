/* readahead.c - preload files into disk cache.
 *
 * Copyright 2013 Rob Landley <rob@landley.net>
 *
 * No standard.

USE_READAHEAD(NEWTOY(readahead, NULL, TOYFLAG_BIN))

config READAHEAD
  bool "readahead"
  default y
  help
    usage: readahead FILE...

    Preload files into disk cache.
*/

#define _LARGEFILE64_SOURCE  // musl's _ALL_SOURCE lies, no off64_t
#include "toys.h"

// glibc won't provide this prototype unless we claim Linux belongs to the FSF
ssize_t readahead(int fd, off64_t offset, size_t count);

static void do_readahead(int fd, char *name)
{
  if (readahead(fd, 0, INT_MAX)) perror_msg("readahead: %s", name);
}

void readahead_main(void)
{
  loopfiles(toys.optargs, do_readahead);
}
