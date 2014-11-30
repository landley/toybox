/* mountpoint.c - Check if a directory is a mountpoint.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_MOUNTPOINT(NEWTOY(mountpoint, "<1qdx[-dx]", TOYFLAG_BIN))

config MOUNTPOINT
  bool "mountpoint"
  default y
  help
    usage: mountpoint [-q] [-d] directory
           mountpoint [-q] [-x] device

    -q	Be quiet, return zero if directory is a mountpoint
    -d	Print major/minor device number of the directory
    -x	Print major/minor device number of the block device
*/

#define FOR_mountpoint
#include "toys.h"

void mountpoint_main(void)
{
  struct stat st1, st2;
  char *arg = *toys.optargs;
  int quiet = toys.optflags & FLAG_q;

  toys.exitval = 1;
  if (((toys.optflags & FLAG_x) ? lstat : stat)(arg, &st1))
    perror_exit("%s", arg);

  if (toys.optflags & FLAG_x) {
    if (S_ISBLK(st1.st_mode)) {
      if (!quiet) printf("%u:%u\n", major(st1.st_rdev), minor(st1.st_rdev));
      toys.exitval = 0;
      return;
    }
    if (!quiet) printf("%s: not a block device\n", arg);
    return;
  }

  // Ignore the fact a file can be a mountpoint for --bind mounts.
  if (!S_ISDIR(st1.st_mode)) {
    if (!quiet) printf("%s: not a directory\n", arg);
    return;
  }

  arg = xmprintf("%s/..", arg);
  xstat(arg, &st2);
  if (CFG_TOYBOX_FREE) free(arg);

  // If the device is different, it's a mount point. If the device _and_
  // inode are the same, it's probably "/". This misses --bind mounts from
  // elsewhere in the same filesystem, but so does the other one and in the
  // absence of a spec I guess that's the expected behavior?
  toys.exitval = !(st1.st_dev != st2.st_dev || st1.st_ino == st2.st_ino);
  if (toys.optflags & FLAG_d)
    printf("%u:%u\n", major(st1.st_dev), minor(st1.st_dev));
  else if (!quiet)
    printf("%s is %sa mountpoint\n", *toys.optargs, toys.exitval ? "not " : "");
}
