/* fsync.c - Synchronize a file's in-core state with storage device.
 *
 * Copyright 2015 Ranjan Kumar <ranjankumar.bth@gmail.comi>
 *
 * No Standard.

USE_FSYNC(NEWTOY(fsync, "<1d", TOYFLAG_BIN))

config FSYNC
  bool "fsync"
  default y
  help
    usage: fsync [-d] [FILE...]

    Synchronize a file's in-core state with storage device.

    -d	Avoid syncing metadata
*/

#define FOR_fsync
#include "toys.h"

static void do_fsync(int fd, char *name)
{
  if (((toys.optflags & FLAG_d) ? fdatasync(fd) : fsync(fd)))
    perror_msg("can't sync '%s'", name);
}

void fsync_main(void)
{
  loopfiles_rw(toys.optargs, O_RDONLY|O_NOATIME|O_NOCTTY|O_CLOEXEC|WARN_ONLY,
      0, do_fsync);
}
