/* df.c - report free disk space.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/df.html

USE_DF(NEWTOY(df, "HPkht*a[-HPkh]", TOYFLAG_SBIN))

config DF
  bool "df"
  default y
  help
    usage: df [-HPkh] [-t type] [FILESYSTEM ...]

    The "disk free" command shows total/used/available disk space for
    each filesystem listed on the command line, or all currently mounted
    filesystems.

    -a	Show all (including /proc and friends)
    -P	The SUSv3 "Pedantic" option
    -k	Sets units back to 1024 bytes (the default without -P)
    -h	Human readable output (K=1024)
    -H	Human readable output (k=1000)
    -t type	Display only filesystems of this type

    Pedantic provides a slightly less useful output format dictated by Posix,
    and sets the units to 512 bytes instead of the default 1024 bytes.
*/

#define FOR_df
#include "toys.h"

GLOBALS(
  struct arg_list *fstype;

  long units;
  int column_widths[5];
  int header_shown;
)

static void measure_column(int col, const char *s)
{
  size_t len = strlen(s);

  if (TT.column_widths[col] < len) TT.column_widths[col] = len;
}

static void measure_numeric_column(int col, long long n)
{
  snprintf(toybuf, sizeof(toybuf), "%lld", n);
  return measure_column(col, toybuf);
}

static void show_header()
{
  TT.header_shown = 1;

  // The filesystem column is always at least this wide.
  if (TT.column_widths[0] < 14) TT.column_widths[0] = 14;

  if (toys.optflags & (FLAG_H|FLAG_h)) {
    xprintf("%-*s Size  Used Avail Use%% Mounted on\n",
            TT.column_widths[0], "Filesystem");
  } else {
    const char *blocks_label = TT.units == 512 ? "512-blocks" : "1K-blocks";
    const char *use_label = toys.optflags & FLAG_P ? "Capacity" : "Use%";

    measure_column(1, blocks_label);
    measure_column(2, "Used");
    measure_column(3, "Available");
    measure_column(4, use_label);
    xprintf("%-*s %*s %*s %*s %*s Mounted on\n",
            TT.column_widths[0], "Filesystem",
            TT.column_widths[1], blocks_label,
            TT.column_widths[2], "Used",
            TT.column_widths[3], "Available",
            TT.column_widths[4], use_label);

    // For the "Use%" column, the trailing % should be inside the column.
    TT.column_widths[4]--;
  }
}

static void show_mt(struct mtab_list *mt, int measuring)
{
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

  if (measuring) {
    measure_column(0, device);
    measure_numeric_column(1, size);
    measure_numeric_column(2, used);
    measure_numeric_column(3, avail);
  } else {
    if (!TT.header_shown) show_header();

    if (toys.optflags & (FLAG_H|FLAG_h)) {
      char *size_str = toybuf, *used_str = toybuf+64, *avail_str = toybuf+128;
      int hr_flags = (toys.optflags & FLAG_H) ? HR_1000 : 0;

      human_readable(size_str, size, hr_flags);
      human_readable(used_str, used, hr_flags);
      human_readable(avail_str, avail, hr_flags);
      xprintf("%-*s %4s  %4s  %4s % 3lld%% %s\n",
        TT.column_widths[0], device,
        size_str, used_str, avail_str, percent, mt->dir);
    } else xprintf("%-*s %*lld %*lld %*lld %*lld%% %s\n",
        TT.column_widths[0], device,
        TT.column_widths[1], size,
        TT.column_widths[2], used,
        TT.column_widths[3], avail,
        TT.column_widths[4], percent,
        mt->dir);
  }

  if (device != mt->device) free(device);
}

void df_main(void)
{
  struct mtab_list *mt, *mtstart, *mtend;
  int measuring;

  if (toys.optflags & (FLAG_H|FLAG_h)) {
    TT.units = 1;
  } else {
    // Units are 512 bytes if you select "pedantic" without "kilobytes".
    TT.units = toys.optflags & FLAG_P ? 512 : 1024;
  }

  if (!(mtstart = xgetmountlist(0))) return;
  mtend = dlist_terminate(mtstart);

  // If we have a list of filesystems on the command line, loop through them.
  if (*toys.optargs) {
    // Measure the names then output the table.
    for (measuring = 1; measuring >= 0; --measuring) {
      char **next;

      for (next = toys.optargs; *next; next++) {
        struct stat st;

        // Stat it (complain if we can't).
        if (stat(*next, &st)) {
          perror_msg("'%s'", *next);
          continue;
        }

        // Find and display this filesystem.  Use _last_ hit in case of
        // overmounts (which is first hit in the reversed list).
        for (mt = mtend; mt; mt = mt->prev) {
          if (st.st_dev == mt->stat.st_dev
              || (st.st_rdev && (st.st_rdev == mt->stat.st_dev)))
          {
            show_mt(mt, measuring);
            break;
          }
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
            if (!(toys.optflags & FLAG_a)) mt3->stat.st_dev = 0;
            mt3 = mt2;
          } else mt2->stat.st_dev = 0;
        }
      }
    }

    // Measure the names then output the table.
    for (measuring = 1; measuring >= 0; --measuring) {
      // Cosmetic: show filesystems in creation order.
      for (mt = mtstart; mt; mt = mt->next) {
        if (mt->stat.st_dev) show_mt(mt, measuring);
      }
    }
  }

  if (CFG_TOYBOX_FREE) llist_traverse(mtstart, free);
}
