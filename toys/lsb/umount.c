/* umount.c - Unmount a mount point.
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/umount.html
 *
 * Note: -n (/etc/mtab) is obsolete, /proc/mounts replaced it. Neither chroot
 * nor per-process mount namespaces can work sanely with mtab. The kernel
 * tracks mount points now, a userspace application can't do so anymore.

USE_UMOUNT(NEWTOY(umount, "ndDflrat*v[!na]", TOYFLAG_BIN|TOYFLAG_STAYROOT))

config UMOUNT
  bool "umount"
  default y
  help
    usage: umount [-a [-t TYPE[,TYPE...]]] [-vrfD] [DIR...]

    Unmount the listed filesystems.

    -a	Unmount all mounts in /proc/mounts instead of command line list
    -D  Don't free loopback device(s).
    -f  Force unmount.
    -l  Lazy unmount (detach from filesystem now, close when last user does).
    -n	Don't use /proc/mounts
    -r  Remount read only if unmounting fails.
    -t	Restrict "all" to mounts of TYPE (or use "noTYPE" to skip)
    -v	Verbose
*/

#define FOR_umount
#include "toys.h"

GLOBALS(
  struct arg_list *t;

  char *types;
)

// todo
//   borrow df code to identify filesystem?
//   umount -a from fstab
//   umount when getpid() not 0, according to fstab
//   lookup mount: losetup -d, bind, file, block

// TODO
// loopback delete
// fstab -o user

// Realloc *old with oldstring,newstring

void comma_collate(char **old, char *new)
{
  char *temp, *atold = *old;

  // Only add a comma if old string didn't end with one
  if (atold && *atold) {
    char *comma = ",";

    if (atold[strlen(atold)-1] == ',') comma = "";
    temp = xmprintf("%s%s%s", atold, comma, new);
  } else temp = xstrdup(new);
  free (atold);
  *old = temp;
}

// iterate through strings in a comma separated list.
// returns start of next entry or NULL if none
// sets *len to length of entry (not including comma)
// advances *list to start of next entry
char *comma_iterate(char **list, int *len)
{
  char *start = *list, *end;

  if (!*list) return 0;

  if (!(end = strchr(*list, ','))) {
    *len = strlen(*list);
    *list = 0;
  } else *list += (*len = end-start)+1;

  return start;
}

static void do_umount(char *dir, char *dev, int flags)
{
  if (!umount2(dir, flags)) {
    if (toys.optflags & FLAG_v) xprintf("%s unmounted\n", dir);

    // Attempt to disassociate loopback device. This ioctl should be ignored
    // for anything else, because lanana allocated ioctl range 'L' to loopback
    if (dev && !(toys.optflags & FLAG_D)) {
      int lfd = open(dev, O_RDONLY);

      if (lfd != -1) {
        // This is LOOP_CLR_FD, fetching it from headers is awkward
        if (!ioctl(lfd, 0x4C01) && (toys.optflags & FLAG_v))
          xprintf("%s cleared\n", dev);
        close(lfd);
      }
    }

    return;
  }

  if (toys.optflags & FLAG_r) {
    if (!mount("", dir, "", MS_REMOUNT|MS_RDONLY, "")) {
      if (toys.optflags & FLAG_v) xprintf("%s remounted ro\n", dir);
      return;
    }
  }

  perror_msg("%s", dir);
}

void umount_main(void)
{
  char **optargs, *pm = "/proc/mounts";
  struct mtab_list *mlsave = 0, *mlrev = 0, *ml;
  int flags=0;

  if (!toys.optc && !(toys.optflags & FLAG_a))
    error_exit("Need 1 arg or -a");

  if (toys.optflags & FLAG_f) flags |= MNT_FORCE;
  if (toys.optflags & FLAG_l) flags |= MNT_DETACH;

  // Load /proc/mounts and get a reversed list (newest first)
  // We use the list both for -a, and to umount /dev/name or do losetup -d
  if (!(toys.optflags & FLAG_n) && !access(pm, R_OK))
    mlrev = dlist_terminate(mlsave = xgetmountlist(pm));

  // Unmount all: loop through mounted filesystems, skip -t, unmount the rest
  if (toys.optflags & FLAG_a) {
    char *typestr = 0;
    struct arg_list *tal;
    
    for (tal = TT.t; tal; tal = tal->next) comma_collate(&typestr, tal->arg);
    for (ml = mlrev; ml; ml = ml->prev) {
      if (typestr) {
        char *type, *types = typestr;
        int len, skip = strncmp(types, "no", 2);

        for (;;) {
          if (!(type = comma_iterate(&types, &len))) break;
          if (!skip) {
            // If one -t starts with "no", the rest must too
            if (strncmp(type, "no", 2)) error_exit("bad -t");
            if (!strncmp(type+2, ml->type, len-2)) {
              skip = 1;
              break;
            }
          } else if (!strncmp(type, ml->type, len) && !ml->type[len]) {
            skip = 0;
            break;
          }
        }
        if (skip) continue;
      }

      do_umount(ml->dir, ml->device, flags);
    }
    if (CFG_TOYBOX_FREE) {
      free(typestr);
      llist_traverse(mlsave, free);
    }
  // TODO: under what circumstances do we umount non-absolute path?
  } else for (optargs = toys.optargs; *optargs; optargs++) {
    char *abs = xabspath(*optargs, 0);

    for (ml = abs ? mlrev : 0; ml; ml = ml->prev) {
      if (!strcmp(ml->dir, abs)) break;
      if (!strcmp(ml->device, abs)) {
        free(abs);
        abs = ml->dir;
        break;
      }
    }

    do_umount(abs ? abs : *optargs, ml ? ml->device : 0, flags);
    if (ml && abs != ml->dir) free(abs);
  }
}
