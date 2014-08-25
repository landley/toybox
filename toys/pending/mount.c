/* mount.c - mount filesystems
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/mount.html
 * Note: -hV is bad spec, haven't implemented -FsLU yet
 * no mtab (/proc/mounts does it) so -n is NOP.

USE_MOUNT(NEWTOY(mount, "?O:afnrvwt:o*[-rw]", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_STAYROOT))

config MOUNT
  bool "mount"
  default n
  help
    usage: mount [-afFrsvw] [-t TYPE] [-o OPTIONS...] [[DEVICE] DIR]

    Mount new filesystem(s) on directories. With no arguments, display existing
    mounts.

    -a	mount all entries in /etc/fstab (with -t, only entries of that TYPE)
    -O	only mount -a entries that have this option
    -f	fake it (don't actually mount)
    -r	read only (same as -o ro)
    -w	read/write (default, same as -o rw)
    -t	specify filesystem type
    -v	verbose

    OPTIONS is a comma separated list of options, which can also be supplied
    as --longopts.

    This mount autodetects loopback mounts (a file on a directory) and
    bind mounts (file on file, directory on directory), so you don't need
    to say --bind or --loop. You can also "mount -a /path" to mount everything
    in /etc/fstab under /path, even if it's noauto.
*/

#define FOR_mount
#include "toys.h"

GLOBALS(
  struct arg_list *optlist;
  char *type;
  char *bigO;

  unsigned long flags;
  char *opts;
  int okuser;
)

// TODO detect existing identical mount (procfs with different dev name?)
// TODO user, users, owner, group, nofail
// TODO -p (passfd)
// TODO -a -t notype,type2
// TODO --subtree
// TODO --rbind, -R
// TODO make "mount --bind,ro old new" work (implicit -o remount)
// TODO mount -a
// TODO mount -o remount
// TODO fstab: lookup default options for mount
// TODO implement -v

// Strip flags out of comma separated list of options, return flags,.
static long flag_opts(char *new, long flags, char **more)
{
  struct {
    char *name;
    long flags;
  } opts[] = {
    // NOPs (we autodetect --loop and --bind)
    {"loop", 0}, {"bind", 0}, {"defaults", 0}, {"quiet", 0},
    {"user", 0}, {"nouser", 0}, // checked in fstab, ignored in -o
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
    {"remount", MS_REMOUNT}, {"move", MS_MOVE},
    // mand dirsync rec iversion strictatime
  };

  if (new) for (;;) {
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

  if (getuid()) {
    if (TT.okuser) TT.okuser = 0;
    else {
      error_msg("'%s' not user mountable in fstab");
      return;
    }
  }

  // Autodetect bind mount or filesystem type
  if (!type || !strcmp(type, "auto")) {
    struct stat stdev, stdir;

    // file on file or dir on dir is a --bind mount.
    if (!stat(dev, &stdev) && !stat(dir, &stdir)
        && ((S_ISREG(stdev.st_mode) && S_ISREG(stdir.st_mode))
            || (S_ISDIR(stdev.st_mode) && S_ISDIR(stdir.st_mode))))
    {
      flags |= MS_BIND;
    } else fp = xfopen("/proc/filesystems", "r");
  } else if (!strcmp(type, "ignore")) return;
  else if (!strcmp(type, "swap"))
    toys.exitval |= xpclose(xpopen((char *[]){"swapon", "--", dev, 0}, 0), 0);

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

    // Trying to autodetect loop mounts like bind mounts above (file on dir)
    // isn't good enough because "mount -t ext2 fs.img dir" is valid, but if
    // you _do_ accept loop mounts with -t how do you tell "-t cifs" isn't
    //  looking for a block device if it's not in /proc/filesystems yet
    // because the module that won't be loaded until you try the mount, and
    // if you can't then DEVICE existing as a file would cause a false
    // positive loopback mount (so "touch servername" becomes a potential
    // denial of service attack...)
    //
    // Solution: try the mount, let the kernel tell us it wanted a block device,
    // then do the loopback setup and retry the mount.
    if (rc && errno == ENOTBLK) {
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

        continue;
      } else error_msg("losetup failed %d", len);
    }

    if (!fp || (rc && errno != EINVAL)) break;
    free(buf);
  }
  if (fp) fclose(fp);

  if (rc) perror_msg("'%s' on '%s'", dev, dir);
}

void mount_main(void)
{
  char *opts = 0, *dev = 0, *dir = 0, **ss;
  long flags = MS_SILENT;
  struct arg_list *o;
  struct mtab_list *mtl, *mm;

// TODO what do mount -aw and -ar do?
  for (o = TT.optlist; o; o = o->next) flags = flag_opts(o->arg, flags, &opts);
  if (toys.optflags & FLAG_r) flags |= MS_RDONLY;
  if (toys.optflags & FLAG_w) flags &= ~MS_RDONLY;

  // Treat each --option as -o option
  for (ss = toys.optargs; *ss; ss++) {
    if ((*ss)[0] && (*ss)[1]) flags = flag_opts(2+*ss, flags, &opts);
    else if (!dev) dev = *ss;
    else if (!dir) dir = *ss;
    // same message as lib/args.c ">2" which we can't use because --opts count
    else error_exit("Max 2 arguments\n");
  }

  if ((toys.optflags & FLAG_a) && dir) error_exit("-a with DIR");

  // Do we need to do an /etc/fstab trawl?
  if (toys.optflags & FLAG_a || !dir || getpid()) {
    for (mtl = xgetmountlist("/etc/fstab"); mtl && (mm = dlist_pop(&mtl));
         free(mm))
    {
      char *aopts = opts ? xstrdup(opts) : 0;
      int aflags;

      if (toys.optflags & FLAG_a) {
        if (!mountlist_istype(mtl,TT.type) || !comma_scanall(mtl->opts,TT.bigO))
          continue;
        
      } else {
        if (dir && strcmp(dir, mtl->dir)) continue;
        if (dev && strcmp(dev, mtl->device) && (dir || strcmp(dev, mtl->dir)))
          continue;
      }

      // user only counts from fstab, not opts.
      if (comma_scan(mtl->opts, "user", 1)) TT.okuser = 1;
      aflags = flag_opts(mtl->opts, flags, &aopts);

      mount_filesystem(mtl->device, mtl->dir, mtl->type, aflags, aopts);

      free(aopts);
    }
  }

  // show mounts
  if (!dir) {
    for (mtl = xgetmountlist(0); mtl && (mm = dlist_pop(&mtl)); free(mm)) {
      char *s = 0;

      if (TT.type && strcmp(TT.type, mm->type)) continue;
      if (*mm->device == '/') s = xabspath(mm->device, 0);
      xprintf("%s on %s type %s (%s)\n",
              s ? s : mm->device, mm->dir, mm->type, mm->opts);
      free(s);
    }

  // one argument: from fstab, remount, subtree
  } else if (!dev) {
    fprintf(stderr, "not yet\n"); // TODO
    return;
  // two arguments
  } else mount_filesystem(dev, dir, TT.type, flags, opts);
}
