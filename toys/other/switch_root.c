/* switch_root.c - Switch from rootfs/initramfs to another filesystem
 *
 * Copyright 2005 Rob Landley <rob@landley.net>

USE_SWITCH_ROOT(NEWTOY(switch_root, "<2c:h", TOYFLAG_SBIN))

config SWITCH_ROOT
  bool "switch_root"
  default y
  help
    usage: switch_root [-c /dev/console] NEW_ROOT NEW_INIT...

    Use from PID 1 under initramfs to free initramfs, chroot to NEW_ROOT,
    and exec NEW_INIT.

    -c	Redirect console to device in NEW_ROOT
    -h	Hang instead of exiting on failure (avoids kernel panic)
*/

#define FOR_switch_root
#include "toys.h"
#include <sys/vfs.h>

GLOBALS(
  char *c;

  dev_t rootdev;
)

static int del_node(struct dirtree *node)
{
  if (node->st.st_dev == TT.rootdev && dirtree_notdotdot(node)) {
    int flag = 0;
    if (S_ISDIR(node->st.st_mode)) {
      if (!node->again) return DIRTREE_COMEAGAIN;
      flag = AT_REMOVEDIR;
    }
    unlinkat(dirtree_parentfd(node), node->name, flag);
  }

  return 0;
}

void switch_root_main(void)
{
  char *newroot = *toys.optargs, **cmdline = toys.optargs+1;
  struct stat st1, st2;
  struct statfs stfs;
  int console QUIET;

  if (getpid() != 1) error_exit("not pid 1");

  // Root filesystem we're leaving must be ramfs or tmpfs
  if (statfs("/", &stfs) ||
    (stfs.f_type != 0x858458f6 && stfs.f_type != 0x01021994))
  {
    error_msg("not ramfs");
    goto panic;
  }

  // New directory must be different filesystem instance
  if (chdir(newroot) || stat(".", &st1) || stat("/", &st2) ||
    st1.st_dev == st2.st_dev)
  {
    error_msg("bad newroot '%s'", newroot);
    goto panic;
  }
  TT.rootdev=st2.st_dev;

  // trim any / characters from the init cmdline, as we want to test it with
  // stat(), relative to newroot. *cmdline is also used below, but by that
  // point we are in the chroot, so a relative path is still OK.
  while (**cmdline == '/') (*cmdline)++;

  // init program must exist and be an executable file
  if (stat(*cmdline, &st1) || !S_ISREG(st1.st_mode) || !(st1.st_mode&0100)) {
    error_msg("bad init");
    goto panic;
  }

  if (TT.c && -1 == (console = open(TT.c, O_RDWR))) {
    perror_msg("bad console '%s'", TT.c);
    goto panic;
  }
 
  // Ok, enough safety checks: wipe root partition.
  dirtree_read("/", del_node);

  // Fix the appearance of the mount table in the newroot chroot
  if (mount(".", "/", NULL, MS_MOVE, NULL)) {
    perror_msg("mount");
    goto panic;
  }

  // Enter the new root before starting init
  if (chroot(".")) {
    perror_msg("chroot");
    goto panic;
  }

  // Make sure cwd does not point outside of the chroot
  if (chdir("/")) {
    perror_msg("chdir");
    goto panic;
  }

  if (TT.c) {
    int i;
    for (i=0; i<3; i++) if (console != i) dup2(console, i);
    if (console>2) close(console);
  }
  execv(*cmdline, cmdline);
  perror_msg("Failed to exec '%s'", *cmdline);
panic:
  if (toys.optflags & FLAG_h) for (;;) wait(NULL);
}
