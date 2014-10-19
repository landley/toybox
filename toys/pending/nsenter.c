/* nsenter.c - Enter existing namespaces
 *
 * Copyright 2014 andy Lutomirski <luto@amacapital.net>

USE_NSENTER(NEWTOY(nsenter, "<1F(no-fork)t#(target)i:(ipc);m:(mount);n:(net);p:(pid);u:(uts);U:(user);", TOYFLAG_USR|TOYFLAG_BIN))

config NSENTER
  bool "nsenter"
  default n
  help
    usage: nsenter [-t pid] [-F] [-i] [-m] [-n] [-p] [-u] [-U] COMMAND...

    Run COMMAND in a different set of namespaces.

    -T  PID to take namespaces from
    -F  don't fork, even if -p is set

    The namespaces to switch are:

    -i	SysV IPC (message queues, semaphores, shared memory)
    -m	Mount/unmount tree
    -n	Network address, sockets, routing, iptables
    -p	Process IDs and init (will fork unless -F is used)
    -u	Host and domain names
    -U	UIDs, GIDs, capabilities

    Each of those options takes an optional argument giving the path of
    the namespace file (usually in /proc).  This optional argument is
    mandatory unless -t is used.
*/

#define FOR_nsenter
#define _GNU_SOURCE
#include "toys.h"
#include <errno.h>
#include <sched.h>
#include <linux/sched.h>

#define NUM_NSTYPES 6

struct nstype {
  int type;
  const char *name;
};

struct nstype nstypes[NUM_NSTYPES] = {
  {CLONE_NEWUSER, "user"}, /* must be first to allow non-root operation */
  {CLONE_NEWUTS,  "uts"},
  {CLONE_NEWPID,  "pid"},
  {CLONE_NEWNET,  "net"},
  {CLONE_NEWNS,   "mnt"},
  {CLONE_NEWIPC,  "ipc"},
};

GLOBALS(
  char *nsnames[6];
  long targetpid;
)

static void enter_by_name(int idx)
{
  int fd, rc;
  char buf[64];
  char *filename = TT.nsnames[idx];

  if (!(toys.optflags & (1<<idx))) return;

  if (!filename || !*filename) {
    if (!(toys.optflags & (1<<NUM_NSTYPES)))
      error_exit("either -t or an ns filename is required");
    sprintf(buf, "/proc/%ld/ns/%s", TT.targetpid, nstypes[idx].name);
    filename = buf;
  }

  fd = open(filename, O_RDONLY | O_CLOEXEC);
  if (fd == -1) perror_exit(filename);

  rc = setns(fd, nstypes[idx].type);
  if (CFG_TOYBOX_FREE) close(fd);
  if (rc != 0) perror_exit("setns");
}

void nsenter_main(void)
{
  int i;

  for (i = 0; i < NUM_NSTYPES; i++)
    enter_by_name(i);

  if ((toys.optflags & (1<<2)) && !(toys.optflags & 1<<(NUM_NSTYPES+1))) {
    /* changed PID ns and --no-fork wasn't set, so fork. */
    pid_t pid = fork();

    if (pid == -1) {
      perror_exit("fork");
    } else if (pid != 0) {
      while (waitpid(pid, 0, 0) == -1 && errno == EINTR)
        ;
      return;
    }
  }

  xexec_optargs(0);
}
