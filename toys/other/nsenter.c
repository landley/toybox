/* nsenter.c - Enter existing namespaces
 *
 * Copyright 2014 andy Lutomirski <luto@amacapital.net>
 *
 * No standard
 *
 * unshare.c - run command in new context
 *
 * Copyright 2011 Rob Landley <rob@landley.net>
 *
 * No Standard
 *

// Note: flags go in same order (right to left) for shared subset
USE_NSENTER(NEWTOY(nsenter, "<1F(no-fork)t#<1(target)i:(ipc);m:(mount);n:(net);p:(pid);u:(uts);U:(user);", TOYFLAG_USR|TOYFLAG_BIN))
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

config NSENTER
  bool "nsenter"
  default n
  help
    usage: nsenter [-t pid] [-F] [-i] [-m] [-n] [-p] [-u] [-U] COMMAND...

    Run COMMAND in a different set of namespaces.

    -t  PID to take namespaces from    (--target)
    -F  don't fork, even if -p is used (--no-fork)

    The namespaces to switch are:

    -i	SysV IPC: message queues, semaphores, shared memory (--ipc)
    -m	Mount/unmount tree (--mnt)
    -n	Network address, sockets, routing, iptables (--net)
    -p	Process IDs and init, will fork unless -F is used (--pid)
    -u	Host and domain names (--uts)
    -U	UIDs, GIDs, capabilities (--user)

    If -t isn't specified, each namespace argument must provide a path
    to a namespace file, ala "-i=/proc/$PID/ns/ipc"
*/

#define FOR_nsenter
#include "toys.h"
#include <linux/sched.h>
int unshare(int flags);
int setns(int fd, int nstype); 

GLOBALS(
  char *nsnames[6];
  long targetpid;
)

void unshare_main(void)
{
  unsigned flags[]={CLONE_NEWUSER, CLONE_NEWUTS, CLONE_NEWPID, CLONE_NEWNET,
                    CLONE_NEWNS, CLONE_NEWIPC}, f = 0;
  int i, fd;

  // Create new namespace(s)?
  if (CFG_UNSHARE && toys.which->name[0]) {
    for (i = 0; i<ARRAY_LEN(flags); i++)
      if (toys.optflags & (1<<i)) f |= flags[i];

    if (unshare(f)) perror_exit(0);

  // Bind to existing namespace(s)?
  } else if (CFG_NSENTER) {
    char *nsnames = "user\0uts\0pid\0net\0mnt\0ipc";

    for (i = 0; i<ARRAY_LEN(flags); i++) {
      char *filename = TT.nsnames[i];

      if (toys.optflags & (1<<i)) {
        if (!filename || !*filename) {
          if (!(toys.optflags & FLAG_t)) error_exit("need -t or =filename");
          sprintf(toybuf, "/proc/%ld/ns/%s", TT.targetpid, nsnames);
          filename = toybuf;
        }

        if (setns(fd = xopen(filename, O_RDONLY), flags[i]))
          perror_exit("setns");
        close(fd);
      }
      nsnames += strlen(nsnames)+1;
    }

    if ((toys.optflags & FLAG_p) && !(toys.optflags & FLAG_F)) {
      pid_t pid = xfork();

      if (pid) {
        while (waitpid(pid, 0, 0) == -1 && errno == EINTR);
        return;
      }
    }
  }

  xexec(toys.optargs);
}
