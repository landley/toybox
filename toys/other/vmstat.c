/* vmstat.c - Report virtual memory statistics.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * TODO: I have no idea how "system" category is calculated.
 * whatever we're doing isn't matching what other implementations are doing.

USE_VMSTAT(NEWTOY(vmstat, ">2n", TOYFLAG_BIN))

config VMSTAT
  bool "vmstat"
  default y
  help
    usage: vmstat [-n] [DELAY [COUNT]]

    Print virtual memory statistics, repeating each DELAY seconds, COUNT times.
    (With no DELAY, prints one line. With no COUNT, repeats until killed.)

    Show processes running and blocked, kilobytes swapped, free, buffered, and
    cached, kilobytes swapped in and out per second, file disk blocks input and
    output per second, interrupts and context switches per second, percent
    of CPU time spent running user code, system code, idle, and awaiting I/O.
    First line is since system started, later lines are since last line.

    -n	Display the header only once
*/

#define FOR_vmstat
#include "toys.h"

struct vmstat_proc {
  // From /proc/stat (jiffies)
  uint64_t user, nice, sys, idle, wait, irq, sirq, intr, ctxt, running, blocked;
  // From /proc/meminfo (units are kb)
  uint64_t memfree, buffers, cached, swapfree, swaptotal;
  // From /proc/vmstat (units are kb)
  uint64_t io_in, io_out;
  // From /proc/vmstat (units are pages)
  uint64_t swap_in, swap_out;
};

// All the elements of vmstat_proc are the same size, so we can populate it as
// a big array, then read the elements back out by name
static void get_vmstat_proc(struct vmstat_proc *vmstat_proc)
{
  char *vmstuff[] = { "/proc/stat", "cpu ", 0, 0, 0, 0, 0, 0,
    "intr ", "ctxt ", "procs_running ", "procs_blocked ", "/proc/meminfo",
    "MemFree: ", "Buffers: ", "Cached: ", "SwapFree: ", "SwapTotal: ",
    "/proc/vmstat", "pgpgin ", "pgpgout ", "pswpin ", "pswpout " };
  uint64_t *new = (uint64_t *)vmstat_proc;
  char *p = p, *name = name;
  int i, j;

  // We use vmstuff to fill out vmstat_proc as an array of uint64_t:
  //   Strings starting with / are the file to find next entries in
  //   Any other string is a key to search for, with decimal value right after
  //   0 means parse another value on same line as last key

  for (i = 0; i<sizeof(vmstuff)/sizeof(char *); i++) {
    if (!vmstuff[i]) p++;
    else if (*vmstuff[i] == '/') {
      xreadfile(name = vmstuff[i], toybuf, sizeof(toybuf));

      continue;
    } else if (!(p = strafter(toybuf, vmstuff[i]))) goto error;
    if (1 != sscanf(p, "%"PRIu64"%n", new++, &j)) goto error;
    p += j;
  }

  return;

error:
  error_exit("No %sin %s\n", vmstuff[i], name);
}

void vmstat_main(void)
{
  struct vmstat_proc top[2];
  int i, loop_delay = 0, loop_max = 0;
  unsigned loop, rows = (toys.optflags & FLAG_n) ? 0 : 25,
           page_kb = sysconf(_SC_PAGESIZE)/1024;
  char *headers="r\0b\0swpd\0free\0buff\0cache\0si\0so\0bi\0bo\0in\0cs\0us\0"
                "sy\0id\0wa", lengths[] = {2,2,6,6,6,6,4,4,5,5,4,4,2,2,2,2};

  memset(top, 0, sizeof(top));
  if (toys.optc) loop_delay = atolx_range(toys.optargs[0], 0, INT_MAX);
  if (toys.optc > 1) loop_max = atolx_range(toys.optargs[1], 1, INT_MAX) - 1;

  for (loop = 0; !loop_max || loop <= loop_max; loop++) {
    unsigned idx = loop&1, offset = 0, expected = 0;
    uint64_t units, total_hz, *ptr = (uint64_t *)(top+idx),
             *oldptr = (uint64_t *)(top+!idx);

    if (loop && loop_delay) sleep(loop_delay);

    // Print headers
    if (rows>3 && !(loop % (rows-3))) {
      char *header = headers;

      if (isatty(1)) terminal_size(0, &rows);
      else rows = 0;

      printf("procs -----------memory---------- ---swap-- -----io---- -system-- ----cpu----\n");
      for (i=0; i<sizeof(lengths); i++) {
        printf(" %*s"+!i, lengths[i], header);
        header += strlen(header)+1;
      }
      xputc('\n');
    }

    // Read data and combine some fields we display as aggregates
    get_vmstat_proc(top+idx);
    top[idx].running--; // Don't include ourselves
    top[idx].user += top[idx].nice;
    top[idx].sys += top[idx].irq + top[idx].sirq;
    top[idx].swaptotal -= top[idx].swapfree;

    // Collect unit adjustments (outside the inner loop to save time)

    if (!loop) {
      char *s = toybuf;

      xreadfile("/proc/uptime", toybuf, sizeof(toybuf));
      while (*(s++) > ' ');
      sscanf(s, "%"PRIu64, &units);
    } else units = loop_delay;

    // add up user, sys, idle, and wait time used since last time
    // (Already appended nice to user)
    total_hz = 0;
    for (i=0; i<4; i++) total_hz += ptr[i+!!i] - oldptr[i+!!i];

    // Output values in order[]: running, blocked, swaptotal, memfree, buffers,
    // cache, swap_in, swap_out, io_in, io_out, sirq, ctxt, user, sys, idle,wait

    for (i=0; i<sizeof(lengths); i++) {
      char order[] = {9, 10, 15, 11, 12, 13, 18, 19, 16, 17, 6, 8, 0, 2, 3, 4};
      uint64_t out = ptr[order[i]];
      int len;

      // Adjust rate and units
      if (i>5) out -= oldptr[order[i]];
      if (order[i]<7) out = ((out*100) + (total_hz/2)) / total_hz;
      else if (order[i]>17) out = ((out * page_kb)+(units-1))/units;
      else if (order[i]>15) out = ((out)+(units-1))/units;
      else if (order[i]<9) out = (out+(units-1)) / units;

      // If a field was too big to fit in its slot, try to compensate later
      expected += lengths[i] + !!i;
      len = expected - offset - !!i;
      if (len < 0) len = 0;
      offset += printf(" %*"PRIu64+!i, len, out);
    }
    xputc('\n');

    if (!loop_delay) break;
  }
}
