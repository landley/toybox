/* mount.c - mount filesystems
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/mount.html
 * Note: -hV is bad spec, haven't implemented -FsLU yet
 * no mtab (/proc/mounts does it) so -n is NOP.

USE_MOUNT(NEWTOY(mount, "?>2afnrvwt:o*[-rw]", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_STAYROOT))

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

// Strip flags out of comma separated list of options.
// Return flags, 
static long parse_opts(char *new, long flags, char **more)
{
  struct {
    char *name;
    long flags;
  } opts[] = {
    // NOPs (we autodetect --loop and --bind)
    {"loop", 0}, {"bind", 0}, {"defaults", 0}, {"quiet", 0},
//    {"noauto", 0}, {"swap", 0},
    {"ro", MS_RDONLY}, {"rw", ~MS_RDONLY},
    {"nosuid", MS_NOSUID}, {"suid", ~MS_NOSUID},
    {"nodev", MS_NODEV}, {"dev", ~MS_NODEV},
    {"noexec", MS_NOEXEC}, {"exec", ~MS_NOEXEC},
    {"sync", MS_SYNCHRONOUS}, {"async", ~MS_SYNCHRONOUS},
    {"noatime", MS_NOATIME}, {"atime", ~MS_NOATIME},
    {"nodiratime", MS_NODIRATIME}, {"diratime", ~MS_NODIRATIME},
    {"loud", ~MS_SILENT},
    {"shared", MS_SHARED}, {"rshared", MS_SHARED|MS_REC},
    {"slave", MS_SLAVE}, {"rslave", MS_SLAVE|MS_REC},
    {"private", MS_PRIVATE}, {"rprivate", MS_SLAVE|MS_REC},
    {"unbindable", MS_UNBINDABLE}, {"runbindable", MS_UNBINDABLE|MS_REC},
    {"remount", MS_REMOUNT}, {"bind", MS_BIND}, {"move", MS_MOVE},
    // mand dirsync rec iversion strictatime
  };

  for (;;) {
    char *comma = strchr(new, ',');
    int i;

    if (comma) *comma = 0;

    // If we recognize an option, apply flags
    for (i = 0; i < ARRAY_LEN(opts); i++) if (!strcasecmp(opts[i].name, new)) {
      long ll = opts[i].flags;

      if (ll < 0) flags &= ll;
      else flags |= ll;

      break;
    }

    // If we didn't recognize it, keep string version
    if (more && i == ARRAY_LEN(opts)) {
      i = *more ? strlen(*more) : 0;
      *more = xrealloc(*more, i + strlen(new) + 2);
      if (i) (*more)[i++] = ',';
      strcpy(i+*more, new);
    }

    if (!comma) break;
    *comma = ',';
    new = comma + 1;
  }

  return flags;
}

static void mount_filesystem(char *dev, char *dir, char *type,
  unsigned long flags, char *opts)
{
  FILE *fp = 0;
  int rc = EINVAL;

  if (toys.optflags & FLAG_f) return;

  if (!type) {
    struct stat stdev, stdir;

    if (!stat(dev, &stdev) && !stat(dir, &stdir)) {
      if (S_ISREG(stdev.st_mode)) {
        // Loopback mount?
        if (S_ISDIR(stdir.st_mode)) {
          char *losetup[] = {"losetup", "-fs", dev, 0};
          int pipes[2], len;
          pid_t pid;

          if (flags & MS_RDONLY) losetup[1] = "-fsr";
          pid = xpopen(losetup, pipes);
          len = readall(pipes[1], toybuf, sizeof(toybuf)-1);
          if (!xpclose(pid, pipes) && len > 1) {
            if (toybuf[len-1] == '\n') --len;
            toybuf[len] = 0;
            dev = toybuf;
          } else error_msg("losetup failed %d", len);
        } else if (S_ISREG(stdir.st_mode)) flags |= MS_BIND;
      } else if (S_ISDIR(stdev.st_mode) && S_ISDIR(stdir.st_mode))
        flags |= MS_BIND;
    }

    if (!(flags & MS_BIND)) fp = xfopen("/proc/filesystems", "r");
  }

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
    if (toys.optflags & FLAG_v)
      printf("try '%s' type '%s' on '%s'\n", dev, type, dir);
    rc = mount(dev, dir, type, flags, opts);
    if (!fp || (rc && errno != EINVAL)) break;
    free(buf);
  }
  if (fp) fclose(fp);

  if (rc) perror_msg("'%s' on '%s'", dev, dir);
}

void mount_main(void)
{
  long flags = MS_SILENT;
  struct arg_list *o;
  char *opts = 0;

  if (toys.optflags & FLAG_a) {
    fprintf(stderr, "not yet\n");
    return;
  }

  if (toys.optflags & FLAG_r) flags |= MS_RDONLY;
  if (toys.optflags & FLAG_w) flags &= ~MS_RDONLY;
  for (o = TT.optlist; o; o = o->next)
    flags = parse_opts(o->arg, flags, &opts);

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
  } else mount_filesystem(toys.optargs[0], toys.optargs[1], TT.type,
                          flags, opts ? opts : "");
}
