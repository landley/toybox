/* mount.c - mount filesystems
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/mount.html
 * Note: -hV is bad spec, haven't implemented -FsLU yet
 * no mtab (/proc/mounts does it) so -n is NOP.

USE_MOUNT(NEWTOY(mount, ">2afnrvwt:o*[-rw]", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_STAYROOT))

config MOUNT
  bool "mount"
  default n
  help
    usage: mount [-afFrsvw] [-t TYPE] [-o OPTIONS...] [[DEVICE] DIR]

    Mount new filesystem(s) on directories. With no arguments, display existing
    mounts.

    -a	mount all entries in /etc/fstab (with -t, only entries of that TYPE)
    -f	fake it (don't actually mount)
    -r	read only (same as -o ro)
    -w	read/write (default, same as -o rw)
    -t	specify filesystem type
    -v	verbose

    OPTIONS is a comma separated list of options, which can also be supplied
    as --longopts.



config MOUNT_AUTODETECT
  help
    usage: mount

    This mount autodetects loopback mounts (a file on a directory) and
    bind mounts (file on file, directory on directory), so you don't need
    to say --bind or --loop.
*/

#define FOR_mount
#include "toys.h"

GLOBALS(
  struct arg_list *optlist;
  char *type;

  unsigned long flags;
  char *opts;
)

static void do_mount(char *dev, char *dir, char *type, unsigned long flags, char *opts)
{
  FILE *fp = 0;
  int rc = EINVAL;

  if (toys.optflags & FLAG_f) return;

  if (!TT.type) fp = xfopen("/proc/filesystems", "r");

  for (;;) {
    char *buf = 0;

    // If type wasn't specified, try all of them in order.
    if (fp) {
      size_t i;

      if (getline(&buf, &i, fp)<0) break;
      type = buf;
      // skip nodev devices
      if (!isspace(*type)) {
        free(buf);
        continue;
      }
      // trim whitespace
      while (isspace(*type)) type++;
      i = strlen(type);
      if (i) type[i-1] = 0;
    }
    rc = mount(dev, dir, type, flags, opts);
    if (!fp || (rc && errno != EINVAL)) break;
    free(buf);
  }
  if (fp) fclose(fp);

  if (rc) perror_msg("'%s' on '%s'", dev, dir);
}

void mount_main(void)
{
  if (toys.optflags & FLAG_a) {
    fprintf(stderr, "not yet\n");
    return;
  }

  // show mounts
  if (!toys.optc) {
    struct mtab_list *mtl = xgetmountlist(0), *m;

    for (mtl = xgetmountlist(0); mtl && (m = dlist_pop(&mtl)); free(m)) {
      char *s = 0;

      if (TT.type && strcmp(TT.type, m->type)) continue;
      if (*m->device == '/') s = xabspath(m->device, 0);
      xprintf("%s on %s type %s (%s)\n",
              s ? s : m->device, m->dir, m->type, m->opts);
      free(s);
    }

  // one argument: from fstab, remount, subtree
  } else if (toys.optc == 1) {
    fprintf(stderr, "not yet\n");
    return;
  // two arguments
  } else do_mount(toys.optargs[0], toys.optargs[1], TT.type, 0, "");
}
