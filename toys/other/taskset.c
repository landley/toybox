/* taskset.c - Retrieve or set the CPU affinity of a process.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_TASKSET(NEWTOY(taskset, "<1^pa", TOYFLAG_BIN|TOYFLAG_STAYROOT))

config TASKSET
  bool "taskset"
  default y
  help
    usage: taskset [-ap] [mask] [PID | cmd [args...]]

    Launch a new task which may only run on certain processors, or change
    the processor affinity of an exisitng PID.

    Mask is a hex string where each bit represents a processor the process
    is allowed to run on. PID without a mask displays existing affinity.

    -p	Set/get the affinity of given PID instead of a new command.
    -a	Set/get the affinity of all threads of the PID.
*/

#define FOR_taskset
#include "toys.h"

#include <sys/syscall.h>
#define sched_setaffinity(pid, size, cpuset) \
  syscall(__NR_sched_setaffinity, (pid_t)pid, (size_t)size, (void *)cpuset)
#define sched_getaffinity(pid, size, cpuset) \
  syscall(__NR_sched_getaffinity, (pid_t)pid, (size_t)size, (void *)cpuset)

// mask is an array of long, which makes the layout a bit weird on big
// endian systems but as long as it's consistent...

static void do_taskset(pid_t pid, int quiet)
{
  unsigned long *mask = (unsigned long *)toybuf;
  char *s = *toys.optargs, *failed = "failed to %s %d's affinity";
  int i, j, k;

  for (i=0; ; i++) {
    if (!quiet) {
      int j = sizeof(toybuf), flag = 0;

      if (-1 == sched_getaffinity(pid, sizeof(toybuf), (void *)mask))
        perror_exit(failed, "get", pid);

      printf("pid %d's %s affinity mask: ", pid, i ? "new" : "current");

      while (j--) {
        int x = 255 & (mask[j/sizeof(long)] >> (8*(j&(sizeof(long)-1))));

        if (flag) printf("%02x", x);
        else if (x) {
          flag++;
          printf("%x", x);
        }
      }
      putchar('\n');
    }

    if (i || toys.optc < 2) return;

    memset(toybuf, 0, sizeof(toybuf));
    k = strlen(s = *toys.optargs);
    s += k;
    for (j = 0; j<k; j++) {
      unsigned long digit = *(--s) - '0';

      if (digit > 9) digit = 10 + tolower(*s)-'a';
      if (digit > 15) error_exit("bad mask '%s'", *toys.optargs);
      mask[j/(2*sizeof(long))] |= digit << 4*(j&((2*sizeof(long))-1));
    }

    if (-1 == sched_setaffinity(pid, sizeof(toybuf), (void *)mask))
      perror_exit(failed, "set", pid);
  }
}

static int task_callback(struct dirtree *new)
{
  if (!new->parent) return DIRTREE_RECURSE;
  if (isdigit(*new->name)) do_taskset(atoi(new->name), 0);

  return 0;
}

void taskset_main(void)
{
  if (!(toys.optflags & FLAG_p)) {
    if (toys.optc < 2) error_exit("Needs 2 args");
    do_taskset(getpid(), 1);
    xexec(toys.optargs+1);
  } else {
    char *c;
    pid_t pid = strtol(toys.optargs[toys.optc-1], &c, 10);

    if (*c) error_exit("Not int %s", toys.optargs[1]);

    if (toys.optflags & FLAG_a) {
      char buf[33];
      sprintf(buf, "/proc/%ld/task/", (long)pid);
      dirtree_read(buf, task_callback);
    } else do_taskset(pid, 0);
  }
}
