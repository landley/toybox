/* df.c - report free disk space.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/df.html

USE_DF(NEWTOY(df, "Pkt*a", TOYFLAG_USR|TOYFLAG_SBIN))

config DF
  bool "df"
  default y
  help
    usage: df [-t type] [FILESYSTEM ...]

    The "disk free" command shows total/used/available disk space for
    each filesystem listed on the command line, or all currently mounted
    filesystems.

    -t type	Display only filesystems of this type.

config DF_PEDANTIC
  bool "options -P and -k"
  default y
  depends on DF
  help
    usage: df [-Pk]

    -P	The SUSv3 "Pedantic" option
    -k	Sets units back to 1024 bytes (the default without -P)

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
  avail = (block * (getuid() ? mt->statvfs.f_bavail : mt->statvfs.f_bfree))
      / TT.units;
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
  if (CFG_DF_PEDANTIC && (toys.optflags & FLAG_P)) {
    xprintf("%s %lld %lld %lld %lld%% %s\n", device, size, used, avail,
      percent, mt->dir);
  } else {
    xprintf("%s% *lld % 10lld % 9lld % 3lld%% %s\n", device, len,
      size, used, avail, percent, mt->dir);
  }

  if (device != mt->device) free(device);
}

void df_main(void)
{
  struct mtab_list *mt, *mt2, *mtlist;

  // Handle -P and -k
  TT.units = 1024;
  if (CFG_DF_PEDANTIC && (toys.optflags & FLAG_P)) {
    // Units are 512 bytes if you select "pedantic" without "kilobytes".
    if ((toys.optflags&(FLAG_P|FLAG_k)) == FLAG_P) TT.units = 512;
    printf("Filesystem %ld-blocks Used Available Capacity Mounted on\n",
      TT.units);
  } else puts("Filesystem\t1K-blocks\tUsed Available Use% Mounted on");

  mtlist = xgetmountlist(0);

  // If we have a list of filesystems on the command line, loop through them.
  if (*toys.optargs) {
    char **next;

    for(next = toys.optargs; *next; next++) {
      struct stat st;

      // Stat it (complain if we can't).
      if(stat(*next, &st)) {
        perror_msg("`%s'", *next);
        continue;
      }

      // Find and display this filesystem.  Use _last_ hit in case of
      // -- bind mounts.
      mt2 = NULL;
      for (mt = mtlist; mt; mt = mt->next) {
        if (st.st_dev == mt->stat.st_dev) {
          mt2 = mt;
          break;
        }
      }
      show_mt(mt2);
    }
  } else {
    // Get and loop through mount list.

    for (mt = mtlist; mt; mt = mt->next) {
      struct mtab_list *mt2, *mt3;

      if (!mt->stat.st_dev) continue;

      // Filter out overmounts.
      mt3 = mt;
      for (mt2 = mt->next; mt2; mt2 = mt2->next) {
        if (mt->stat.st_dev == mt2->stat.st_dev) {
          // For --bind mounts, take last match
          if (!strcmp(mt->device, mt2->device)) mt3 = mt2;
          // Filter out overmounts
          mt2->stat.st_dev = 0;
        }
      }
      show_mt(mt3);
    }
  }

  if (CFG_TOYBOX_FREE) llist_traverse(mtlist, free);
}
