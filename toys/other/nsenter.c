/* nsenter.c - Enter existing namespaces
 *
 * Copyright 2014 Andy Lutomirski <luto@amacapital.net>
 *
 * See http://man7.org/linux/man-pages/man1/nsenter.1.html
 *
 * unshare.c - run command in new context
 *
 * Copyright 2011 Rob Landley <rob@landley.net>
 *
 * See http://man7.org/linux/man-pages/man1/unshare.1.html
 *

// Note: flags go in same order (right to left) for shared subset
USE_NSENTER(NEWTOY(nsenter, "<1F(no-fork)t#<1(target)i:(ipc);m:(mount);n:(net);p:(pid);u:(uts);U:(user);", TOYFLAG_USR|TOYFLAG_BIN))
USE_UNSHARE(NEWTOY(unshare, "<1^f(fork);r(map-root-user);i:(ipc);m:(mount);n:(net);p:(pid);u:(uts);U:(user);", TOYFLAG_USR|TOYFLAG_BIN))

config UNSHARE
  bool "unshare"
  default y
  depends on TOYBOX_CONTAINER
  help
    usage: unshare [-imnpuUr] COMMAND...

    Create new container namespace(s) for this process and its children, so
    some attribute is not shared with the parent process.

    -f	Fork command in the background (--fork)
    -i	SysV IPC (message queues, semaphores, shared memory) (--ipc)
    -m	Mount/unmount tree (--mount)
    -n	Network address, sockets, routing, iptables (--net)
    -p	Process IDs and init (--pid)
    -r	Become root (map current euid/egid to 0/0, implies -U) (--map-root-user)
    -u	Host and domain names (--uts)
    -U	UIDs, GIDs, capabilities (--user)

    A namespace allows a set of processes to have a different view of the
    system than other sets of processes.

config NSENTER
  bool "nsenter"
  depends on TOYBOX_CONTAINER
  default y
  help
    usage: nsenter [-t pid] [-F] [-i] [-m] [-n] [-p] [-u] [-U] COMMAND...

    Run COMMAND in an existing (set of) namespace(s).

    -t	PID to take namespaces from    (--target)
    -F	don't fork, even if -p is used (--no-fork)

    The namespaces to switch are:

    -i	SysV IPC: message queues, semaphores, shared memory (--ipc)
    -m	Mount/unmount tree (--mount)
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

#define unshare(flags) syscall(SYS_unshare, flags)
#define setns(fd, nstype) syscall(SYS_setns, fd, nstype)

GLOBALS(
  char *Uupnmi[6];
  long t;
)

// Code that must run in unshare's flag context
#define FOR_unshare
#include <generated/flags.h>

static void write_ugid_map(char *map, unsigned eugid)
{
  int fd = xopen(map, O_WRONLY);

  dprintf(fd, "0 %u 1", eugid);
  xclose(fd);
}

static void handle_r(int euid, int egid)
{
  int fd;

  if ((fd = open("/proc/self/setgroups", O_WRONLY)) >= 0) {
    xwrite(fd, "deny", 4);
    close(fd);
  }

  write_ugid_map("/proc/self/uid_map", euid);
  write_ugid_map("/proc/self/gid_map", egid);
}

static int test_r()
{
  return toys.optflags & FLAG_r;
}

static int test_f()
{
  return toys.optflags & FLAG_f;
}

// Shift back to the context GLOBALS lives in (I.E. matching the filename).
#define FOR_nsenter
#include <generated/flags.h>

void unshare_main(void)
{
  unsigned flags[]={CLONE_NEWUSER, CLONE_NEWUTS, CLONE_NEWPID, CLONE_NEWNET,
                    CLONE_NEWNS, CLONE_NEWIPC}, f = 0;
  int i, fd;

  // Create new namespace(s)?
  if (CFG_UNSHARE && *toys.which->name=='u') {
    // For -r, we have to save our original [ug]id before calling unshare()
    int euid = geteuid(), egid = getegid();

    // unshare -U does not imply -r, so we cannot use [+rU]
    if (test_r()) toys.optflags |= FLAG_U;

    for (i = 0; i<ARRAY_LEN(flags); i++)
      if (toys.optflags & (1<<i)) f |= flags[i];

    if (unshare(f)) perror_exit(0);
    if (test_r()) handle_r(euid, egid);

    if (test_f()) {
      toys.exitval = xrun(toys.optargs);

      return;
    }
  // Bind to existing namespace(s)?
  } else if (CFG_NSENTER) {
    char *nsnames = "user\0uts\0pid\0net\0mnt\0ipc";

    for (i = 0; i<ARRAY_LEN(flags); i++) {
      char *filename = TT.Uupnmi[i];

      if (toys.optflags & (1<<i)) {
        if (!filename || !*filename) {
          if (!(toys.optflags & FLAG_t)) error_exit("need -t or =filename");
          sprintf(toybuf, "/proc/%ld/ns/%s", TT.t, nsnames);
          filename = toybuf;
        }

        if (setns(fd = xopenro(filename), flags[i])) perror_exit("setns");
        close(fd);
      }
      nsnames += strlen(nsnames)+1;
    }

    if ((toys.optflags & FLAG_p) && !(toys.optflags & FLAG_F)) {
      toys.exitval = xrun(toys.optargs);

      return;
    }
  }

  xexec(toys.optargs);
}

void nsenter_main(void)
{
  unshare_main();
}
