/* umount.c - Unmount a mount point.
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/umount.html
 *
 * Note: -n (/etc/mtab) is obsolete, /proc/mounts replaced it. Neither chroot
 * nor per-process mount namespaces can work sanely with mtab. The kernel
 * tracks mount points now, a userspace application can't do so anymore.

USE_UMOUNT(NEWTOY(umount, "ndDflrat*v", TOYFLAG_BIN|TOYFLAG_STAYROOT))

config UMOUNT
  bool "umount"
  default y
  help
    usage: umount [-a [-t TYPE[,TYPE...]]] [-vrfD] [DIR...]

    Unmount the listed filesystems.

    -a	Unmount all mounts in /proc/mounts instead of command line list
    -t	Restrict "all" to mounts of TYPE (or use "noTYPE" to skip)
    -D  Don't free loopback device(s).
    -f  Force unmount.
    -l  Lazy unmount (detach from filesystem now, close when last user does).
    -r  Remount read only if unmounting fails.
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

void arg_comma_collate(char **old, struct arg_list *arg)
{
  while (arg) {
    comma_collate(old, arg->arg);
    arg = arg->next;
  }
}

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

static void do_umount(char *dir, int flags)
{
  if (!umount2(dir, flags)) {
    if (toys.optflags & FLAG_v) printf("%s unmounted", dir);
    return;
  }
  if (toys.optflags & FLAG_r) {
    if (!mount("", dir, "", MS_REMOUNT|MS_RDONLY, "")) {
      if (toys.optflags & FLAG_v) printf("%s remounted ro", dir);
      return;
    }
  }
  perror_msg("%s", dir);
}

void umount_main(void)
{
  int flags=0;
  char **optargs;

  if (!toys.optc && !(toys.optflags & FLAG_a))
    error_exit("Need 1 arg or -a");

  if (toys.optflags & FLAG_f) flags |= MNT_FORCE;
  if (toys.optflags & FLAG_l) flags |= MNT_DETACH;

  for (optargs = toys.optargs; *optargs; optargs++) do_umount(*optargs, flags);

  if (toys.optflags & FLAG_a) {
    struct mtab_list *mlsave, *ml;
    char *typestr = 0;

    if (TT.t) arg_comma_collate(&typestr, TT.t);

    // Loop through mounted filesystems
    for (mlsave = ml = xgetmountlist(0); ml; ml = ml->next) {
      if (TT.t) {
        char *type, *types = typestr;
        int len, skip = strncmp(types, "no", 2);

        // Loop through -t filters
        for (;;) {
          if (!(type = comma_iterate(&types, &len))) break;
          if (!skip) {
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
      do_umount(ml->dir, flags);
    }
    if (CFG_TOYBOX_FREE) {
      free(typestr);
      llist_traverse(mlsave, free);
    }

    return;
  }
}
