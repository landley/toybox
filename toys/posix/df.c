/* df.c - report free disk space.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/df.html

USE_DF(NEWTOY(df, "Pkt*a[-Pk]", TOYFLAG_SBIN))

config DF
  bool "df"
  default y
  help
    usage: df [-t type] [FILESYSTEM ...]

    The "disk free" command shows total/used/available disk space for
    each filesystem listed on the command line, or all currently mounted
    filesystems.

    -P	The SUSv3 "Pedantic" option
    -k	Sets units back to 1024 bytes (the default without -P)
    -t type	Display only filesystems of this type.

    Pedantic provides a slightly less useful output format dictated by Posix,
    and sets the units to 512 bytes instead of the default 1024 bytes.
*/

#define FOR_df
#include "toys.h"

GLOBALS(
  struct arg_list *fstype;

  long units;
)

static void show_mt(struct mtab_list *mt)
{
  int len;
  long long size, used, avail, percent, block;
  char *device;

  // Return if it wasn't found (should never happen, but with /etc/mtab...)
  if (!mt) return;

  // If we have -t, skip other filesystem types
  if (TT.fstype) {
    struct arg_list *al;

    for (al = TT.fstype; al; al = al->next) 
      if (!strcmp(mt->type, al->arg)) break;

    if (!al) return;
  }

  // If we don't have -a, skip synthetic filesystems
  if (!(toys.optflags & FLAG_a) && !mt->statvfs.f_blocks) return;

  // Figure out how much total/used/free space this filesystem has,
  // forcing 64-bit math because filesystems are big now.
  block = mt->statvfs.f_bsize ? mt->statvfs.f_bsize : 1;
  size = (block * mt->statvfs.f_blocks) / TT.units;
  used = (block * (mt->statvfs.f_blocks-mt->statvfs.f_bfree)) / TT.units;
  avail = (block*(getuid()?mt->statvfs.f_bavail:mt->statvfs.f_bfree))/TT.units;
  if (!(used+avail)) percent = 0;
  else {
    percent = (used*100)/(used+avail);
    if (used*100 != percent*(used+avail)) percent++;
  }

  device = *mt->device == '/' ? realpath(mt->device, NULL) : NULL;
  if (!device) device = mt->device;

  // Figure out appropriate spacing
  len = 25 - strlen(device);
  if (len < 1) len = 1;
  xprintf("%s% *lld % 10lld % 10lld % *lld%% %s\n", device, len,
    size, used, avail, (toys.optflags & FLAG_P) ? 7 : 3, percent, mt->dir);

  if (device != mt->device) free(device);
}

void df_main(void)
{
  struct mtab_list *mt, *mtstart, *mtend;
  int p = toys.optflags & FLAG_P;

  // Units are 512 bytes if you select "pedantic" without "kilobytes".
  TT.units = p ? 512 : 1024;
  xprintf("Filesystem%8s-blocks\tUsed  Available %s Mounted on\n",
    p ? "512" : "1K", p ? "Capacity" : "Use%");

  if (!(mtstart = xgetmountlist(0))) return;
  mtend = dlist_terminate(mtstart);

  // If we have a list of filesystems on the command line, loop through them.
  if (*toys.optargs) {
    char **next;

    for(next = toys.optargs; *next; next++) {
      struct stat st;

      // Stat it (complain if we can't).
      if(stat(*next, &st)) {
        perror_msg("'%s'", *next);
        continue;
      }

      // Find and display this filesystem.  Use _last_ hit in case of
      // overmounts (which is first hit in the reversed list).
      for (mt = mtend; mt; mt = mt->prev) {
        if (st.st_dev == mt->stat.st_dev
            || (st.st_rdev && (st.st_rdev == mt->stat.st_dev)))
        {
          show_mt(mt);
          break;
        }
      }
    }
  } else {
    // Loop through mount list to filter out overmounts.
    for (mt = mtend; mt; mt = mt->prev) {
      struct mtab_list *mt2, *mt3;

      // 0:0 is LANANA null device
      if (!mt->stat.st_dev) continue;

      // Filter out overmounts.
      mt3 = mt;
      for (mt2 = mt->prev; mt2; mt2 = mt2->prev) {
        if (mt->stat.st_dev == mt2->stat.st_dev) {
          // For --bind mounts, show earliest mount
          if (!strcmp(mt->device, mt2->device)) {
            if (!toys.optflags & FLAG_a) mt3->stat.st_dev = 0;
            mt3 = mt2;
          } else mt2->stat.st_dev = 0;
        }
      }
    }
    // Cosmetic: show filesystems in creation order
    for (mt = mtstart; mt; mt = mt->next) if (mt->stat.st_dev) show_mt(mt);
  }

  if (CFG_TOYBOX_FREE) llist_traverse(mtstart, free);
}
