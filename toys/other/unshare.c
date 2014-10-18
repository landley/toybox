/* unshare.c - run command in new context
 *
 * Copyright 2011 Rob Landley <rob@landley.net>

USE_UNSHARE(NEWTOY(unshare, "<1^imnpuU", TOYFLAG_USR|TOYFLAG_BIN))

config UNSHARE
  bool "unshare"
  default y
  depends on TOYBOX_CONTAINER
  help
    usage: unshare [-imnpuU] COMMAND...

    Create new namespace(s) for this process and its children, so some
    attribute is not shared with the parent process.  This is part of
    Linux Containers.  Each process can have its own:

    -i	SysV IPC (message queues, semaphores, shared memory)
    -m	Mount/unmount tree
    -n	Network address, sockets, routing, iptables
    -p	Process IDs and init
    -u	Host and domain names
    -U	UIDs, GIDs, capabilities
*/

#include "toys.h"
#include <linux/sched.h>
extern int unshare (int __flags);

void unshare_main(void)
{
  unsigned flags[]={CLONE_NEWUSER, CLONE_NEWUTS, CLONE_NEWPID, CLONE_NEWNET,
                    CLONE_NEWNS, CLONE_NEWIPC, 0};
  unsigned f=0;
  int i;

  for (i=0; flags[i]; i++) if (toys.optflags & (1<<i)) f |= flags[i];

  if (unshare(f)) perror_exit("failed");

  xexec_optargs(0);
}
