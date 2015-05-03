/* dmesg.c - display/control kernel ring buffer.
 *
 * Copyright 2006, 2007 Rob Landley <rob@landley.net>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/dmesg.html

// We care that FLAG_c is 1, so keep c at the end.
USE_DMESG(NEWTOY(dmesg, "trs#<1n#c[!tr]", TOYFLAG_BIN))

config DMESG
  bool "dmesg"
  default y
  help
    usage: dmesg [-c] [-r|-t] [-n LEVEL] [-s SIZE]

    Print or control the kernel ring buffer.

    -c	Clear the ring buffer after printing
    -n	Set kernel logging LEVEL (1-9)
    -r	Raw output (with <level markers>)
    -s	Show the last SIZE many bytes
    -t	Don't print kernel's timestamps
*/

#define FOR_dmesg
#include "toys.h"
#include <sys/klog.h>

GLOBALS(
  long level;
  long size;
)

void dmesg_main(void)
{
  // For -n just tell kernel to which messages to keep.
  if (toys.optflags & FLAG_n) {
    if (klogctl(8, NULL, TT.level)) perror_exit("klogctl");
  } else {
    char *data, *to, *from;
    int size;

    // Figure out how much data we need, and fetch it.
    size = TT.size;
    if (!size && 1>(size = klogctl(10, 0, 0))) perror_exit("klogctl");;
    data = to = from = xmalloc(size+1);
    size = klogctl(3 + (toys.optflags & FLAG_c), data, size);
    if (size < 0) perror_exit("klogctl");
    data[size] = 0;

    // Filter out level markers and optionally time markers
    if (!(toys.optflags & FLAG_r)) while ((from - data) < size) {
      if (from == data || from[-1] == '\n') {
        char *to;

        if (*from == '<' && (to = strchr(from, '>'))) from = ++to;
        if ((toys.optflags&FLAG_t) && *from == '[' && (to = strchr(from, ']')))
          from = to+1+(to[1]==' ');
      }
      *(to++) = *(from++);
    } else to = data+size;

    // Write result. The odds of somebody requesting a buffer of size 3 and
    // getting "<1>" are remote, but don't segfault if they do.
    if (to != data) {
      xwrite(1, data, to-data);
      if (to[-1] != '\n') xputc('\n');
    }
    if (CFG_TOYBOX_FREE) free(data);
  }
}
