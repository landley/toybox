/* taskset.c - Retrieve or set the CPU affinity of a process.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_TASKSET(NEWTOY(taskset, "^p(pid)a(all-tasks)c(cpu-list)", TOYFLAG_USR|TOYFLAG_BIN))
USE_NPROC(NEWTOY(nproc, "a(all)", TOYFLAG_USR|TOYFLAG_BIN))

config NPROC
  bool "nproc"
  default y
  help
    usage: nproc [-a]

    Print number of processors.

    -a	Show all processors, not just ones this task can run on (--all)

config TASKSET
  bool "taskset"
  default y
  help
    usage: taskset [-apc] [mask] [PID | cmd [args...]]

    Launch a new task which may only run on certain processors, or change
    the processor affinity of an existing PID.

    The mask may be specified as  a hex string where each bit represents a
    processor the process is allowed to run on, or as a CPU list with the
    -c option. PID without a mask displays existing affinity. A PID of zero
    means the taskset process.

    -p	Set/get affinity of given PID instead of a new command (--pid)
    -a	Set/get affinity of all threads of the PID (--all-tasks)
    -c	Specify mask as a cpu list, for example 1,3,4-8:2 (--cpu-list)
*/

#define FOR_taskset
#include "toys.h"

// mask is array of long which makes layout a bit weird on big endian systems
#define sched_setaffinity(pid, size, cpuset) \
  syscall(__NR_sched_setaffinity, (pid_t)pid, (size_t)size, (void *)cpuset)
#define sched_getaffinity(pid, size, cpuset) \
  syscall(__NR_sched_getaffinity, (pid_t)pid, (size_t)size, (void *)cpuset)

#define TOYBUF_BITS (8*sizeof(toybuf))

static int find_next_cpu(unsigned long *mask, int i)
{
  for (; i < TOYBUF_BITS; i++)
    if (mask[i/(8*sizeof(long))] & (1UL << (i%(8*sizeof(long)))))
      return i;
  return i;
}

static void do_taskset(pid_t pid)
{
  unsigned long *mask = (unsigned long *)toybuf;
  char *s, *failed = "failed to %s pid %d's affinity";
  int i, j, k;

  // loop through twice to display before/after affinity masks
  for (i=0; ; i++) {
    if (FLAG(p) || !toys.optc) {
      if (-1 == sched_getaffinity(pid, sizeof(toybuf), (void *)mask))
        perror_exit(failed, "get", pid);

      if (toys.optc)
        printf("pid %d's %s affinity mask: ", pid, i ? "new" : "current");

      if (FLAG(c)) {
        // Print as cpu list
        int next = 0;  // where to start looking for next cpu
        int a;  // start of range

        while ((a = find_next_cpu(mask, next)) < TOYBUF_BITS) {
          // next is 0 only on first iteration.
          if (next > 0) putchar(',');
          next = find_next_cpu(mask, a + 1);
          if (next == TOYBUF_BITS) {
            printf("%d", a);
          } else {
            // Extend range as long as the step size is the same.
            int b = next, step = b - a;
            while ((next = find_next_cpu(mask, b + 1)) < TOYBUF_BITS
                   && next - b == step) {
              b = next;
            }
            if (b - a == step) {
              // Only print one CPU; try to put the next CPU in the next range.
              printf("%d", a);
              next = b;
            } else if (step == 1) {
              printf("%d-%d", a, b);
            } else {
              printf("%d-%d:%d", a, b, step);
            }
          }
        }
      } else {
        // Print as mask
        for (j = sizeof(toybuf)/sizeof(long), k = 0; --j>=0;) {
          if (k) printf("%0*lx", (int)(2*sizeof(long)), mask[j]);
          else if (mask[j]) {
            k++;
            printf("%lx", mask[j]);
          }
        }
      }
      putchar('\n');
    }

    if (i || toys.optc < 2) return;

    memset(toybuf, 0, sizeof(toybuf));
    if (FLAG(c)) {
      // Convert cpu list to mask[] bits
      char* failed_cpu_list = "failed to parse CPU list: %s";
      s = *toys.optargs;
      do {
        if (s != *toys.optargs) {
          if (*s != ',') {
            error_exit(failed_cpu_list, *toys.optargs);
          }
          s++;
        }

        // Parse a[-b[:step]].
        int a, b, step = 1;
        int nc = 0;
        int n = sscanf(s, "%d%n-%d%n:%d%n", &a, &nc, &b, &nc, &step, &nc);

        if (n == 1) {
          b = a;
        }

        // Reject large steps to avoid overflow in loop below.
        if (n<=0 || a<0 || b>=TOYBUF_BITS || b<a
            || step<1 || step>=TOYBUF_BITS) {
          error_exit(failed_cpu_list, *toys.optargs);
        }

        // Since `nc` is updated before [-:] is parsed, any other char will
        // be left in `s` and cause a parse error in the next iteration.
        s += nc;

        for (j = a; j <= b; j += step) {
          mask[j/(8*sizeof(long))] |= 1UL << (j%(8*sizeof(long)));
        }
      } while (*s);
    } else {
      // Convert hex string to mask[] bits.
      j = (k = strlen(s = *toys.optargs))-2*sizeof(toybuf);
      if (j>0) {
        s += j;
        k -= j;
      }
      s += k;
      for (j = 0; j<k; j++) {
        unsigned long digit = *(--s) - '0';

        if (digit > 9) digit = 10 + tolower(*s)-'a';
        if (digit > 15) error_exit("bad mask '%s'", *toys.optargs);
        mask[j/(2*sizeof(long))] |= digit << 4*(j&((2*sizeof(long))-1));
      }
    }

    if (-1 == sched_setaffinity(pid, sizeof(toybuf), (void *)mask))
      perror_exit(failed, "set", pid);
  }
}

static int task_callback(struct dirtree *new)
{
  if (!new->parent) return DIRTREE_RECURSE|DIRTREE_SHUTUP|DIRTREE_PROC;
  do_taskset(atoi(new->name));

  return 0;
}

void taskset_main(void)
{
  if (!FLAG(p) && toys.optc) {
    if (toys.optc<2) error_exit("Needs 2 args");
    do_taskset(getpid());
    xexec(toys.optargs+1);
  } else {
    char *c, buf[33];
    pid_t pid = getpid();

    if (toys.optc) {
      pid = strtol(toys.optargs[toys.optc-1], &c, 10);
      if (*c) error_exit("Not int %s", toys.optargs[toys.optc-1]);
    }

    if (FLAG(a)) {
      sprintf(buf, "/proc/%ld/task/", (long)pid);
      dirtree_read(buf, task_callback);
    } else do_taskset(pid);
  }
}

void nproc_main(void)
{
  unsigned i, j, nproc = 0;
  DIR *dd;

  // This can only detect 32768 processors. Call getaffinity and count bits.
  if (!toys.optflags && -1!=sched_getaffinity(getpid(), 4096, toybuf))
    for (i = 0; i<4096; i++)
      if (toybuf[i]) for (j=0; j<8; j++) if (toybuf[i]&(1<<j)) nproc++;

  // If getaffinity failed or --all, count cpu entries in sysfs
  // (/proc/cpuinfo filters out hot-unplugged CPUs, sysfs doesn't)
  if (!nproc && (dd = opendir("/sys/devices/system/cpu"))) {
    struct dirent *de;
    char *ss;

    while ((de = readdir(dd))) {
      if (smemcmp(de->d_name, "cpu", 3)) continue;
      for (ss = de->d_name+3; isdigit(*ss); ss++);
      if (!*ss) nproc++;
    }
    closedir(dd);
  }

  xprintf("%u\n", nproc ? : 1);
}
