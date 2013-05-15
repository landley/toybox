/* getmountlist.c - Get a linked list of mount points, with stat information.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

#include <mntent.h>

// Get list of mounted filesystems, including stat and statvfs info.
// Returns a reversed list, which is good for finding overmounts and such.

struct mtab_list *xgetmountlist(void)
{
  struct mtab_list *mtlist, *mt;
  struct mntent *me;
  FILE *fp;

  if (!(fp = setmntent("/proc/mounts", "r"))) perror_exit("bad /proc/mounts");

  // The "test" part of the loop is done before the first time through and
  // again after each "increment", so putting the actual load there avoids
  // duplicating it. If the load was NULL, the loop stops.

  for (mtlist = 0; (me = getmntent(fp)); mtlist = mt) {
    mt = xzalloc(sizeof(struct mtab_list) + strlen(me->mnt_fsname) +
      strlen(me->mnt_dir) + strlen(me->mnt_type) + 3);
    mt->next = mtlist;

    // Collect details about mounted filesystem (don't bother for /etc/fstab).
    stat(me->mnt_dir, &(mt->stat));
    statvfs(me->mnt_dir, &(mt->statvfs));

    // Remember information from /proc/mounts
    mt->dir = stpcpy(mt->type, me->mnt_type) + 1;
    mt->device = stpcpy(mt->dir, me->mnt_dir) + 1;
    strcpy(mt->device, me->mnt_fsname);
  }
  endmntent(fp);

  return mtlist;
}
