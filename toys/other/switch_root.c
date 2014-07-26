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
  char *console;

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
  int console = console; // gcc's "may be used" warnings are broken.

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

  // init program must exist and be an executable file
  if (stat("init", &st1) || !S_ISREG(st1.st_mode) || !(st1.st_mode&0100)) {
    error_msg("bad init");
    goto panic;
  }

  if (TT.console && -1 == (console = open(TT.console, O_RDWR))) {
    perror_msg("bad console '%s'", TT.console);
    goto panic;
  }
 
  // Ok, enough safety checks: wipe root partition.
  dirtree_read("/", del_node);

  if (TT.console) {
    int i;
    for (i=0; i<3; i++) if (console != i) dup2(console, i);
    if (console>2) close(console);
  }
  execv(*cmdline, cmdline);
  perror_msg("Failed to exec '%s'", *cmdline);
panic:
  if (toys.optflags & FLAG_h) for (;;) wait(NULL);
}
