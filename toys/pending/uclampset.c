/* uclampset.c - Set/get processes' utilization clamping attributes.
 *
 * Copyright 2021 The Android Open Source Project
 *
 * No standard. See https://man7.org/linux/man-pages/man1/uclampset.1.html

USE_TASKSET(NEWTOY(uclampset, "p#am#M#R", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_STAYROOT))

config UCLAMPSET
  bool "uclampset"
  default y
  help
    usage: uclampset [-p PID] [-m MIN] [-M MAX] [CMD [ARG...]]

    Set/get utilization clamping attributes for new or existing processes.

    -a	Apply to all the tasks/threads for the given PID
    -m MIN	Set the minimum
    -M MAX	Set the maximum
    -p PID	Apply to PID rather than launching a command
    -R	Set SCHED_FLAG_RESET_ON_FORK
*/

#define FOR_uclampset
#include "toys.h"

GLOBALS(
  long M, m, p;

  int set;
)

#include <sys/syscall.h>
// The uclamp flags and fields in struct sched_attr were added to the kernel
// in 2019, so we can't rely on the headers yet.
#define SCHED_FLAG_RESET_ON_FORK 0x01
#define SCHED_FLAG_KEEP_POLICY 0x08
#define SCHED_FLAG_KEEP_PARAMS 0x10
#define SCHED_FLAG_UTIL_CLAMP_MIN 0x20
#define SCHED_FLAG_UTIL_CLAMP_MAX 0x40
#define sched_getattr(pid, attr) \
  syscall(__NR_sched_getattr, pid, attr, sizeof(struct sched_attr_v2), 0)
#define sched_setattr(pid, attr, flags) \
  syscall(__NR_sched_setattr, pid, attr, flags)
struct sched_attr_v2 {
  unsigned size;
  unsigned sched_policy;
  unsigned long long sched_flags;
  int sched_nice;
  unsigned sched_priority;
  unsigned long long sched_runtime;
  unsigned long long sched_deadline;
  unsigned long long sched_period;
  // These fields aren't in v1 of the struct.
  unsigned sched_util_min;
  unsigned sched_util_max;
};

static void do_uclampset(pid_t pid)
{
  struct sched_attr_v2 *sa = (struct sched_attr_v2 *)toybuf;

  if (TT.set) {
    if (sched_setattr(pid, sa, 0)) perror_exit("sched_setattr for pid %d", pid);
  } else {
    char path[33], *comm;
    size_t len;

    if (sched_getattr(pid, sa)) perror_exit("sched_getattr for pid %d", pid);
    sprintf(path, "/proc/%u/comm", pid);
    comm = xreadfile(path, 0, 0);
    len = strlen(comm);
    if (comm[len-1] == '\n') comm[len-1] = 0;
    printf("%s (%d) util_clamp: min: %u max: %u\n", comm, pid,
           sa->sched_util_min, sa->sched_util_max);
    free(comm);
  }
}

static int task_callback(struct dirtree *new)
{
  if (!new->parent) return DIRTREE_RECURSE;
  if (isdigit(*new->name)) do_uclampset(atoi(new->name));

  return 0;
}

void uclampset_main(void)
{
  struct sched_attr_v2* sa = (struct sched_attr_v2 *)toybuf;

  memset(sa, 0, sizeof(*sa));
  sa->size = sizeof(*sa);
  sa->sched_flags = SCHED_FLAG_KEEP_POLICY | SCHED_FLAG_KEEP_PARAMS;
  if (FLAG(R)) {
    sa->sched_flags |= SCHED_FLAG_RESET_ON_FORK;
    TT.set = 1;
  }
  if (FLAG(m)) {
    sa->sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN;
    sa->sched_util_min = TT.m;
    TT.set = 1;
  }
  if (FLAG(M)) {
    sa->sched_flags |= SCHED_FLAG_UTIL_CLAMP_MAX;
    sa->sched_util_max = TT.M;
    TT.set = 1;
  }

  if (!FLAG(p)) {
    if (toys.optc < 1) error_exit("Need -p PID or CMD [ARG...]");
    do_uclampset(getpid());
    xexec(toys.optargs);
  } else if (FLAG(a)) {
    char buf[33];
    sprintf(buf, "/proc/%ld/task/", TT.p);
    dirtree_read(buf, task_callback);
  } else do_uclampset(TT.p);
}
