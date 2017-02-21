/* dmesg.c - display/control kernel ring buffer.
 *
 * Copyright 2006, 2007 Rob Landley <rob@landley.net>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/dmesg.html

// We care that FLAG_c is 1, so keep c at the end.
USE_DMESG(NEWTOY(dmesg, "w(follow)Ctrs#<1n#c[!tr][!Cc]", TOYFLAG_BIN))

config DMESG
  bool "dmesg"
  default n
  help
    usage: dmesg [-Cc] [-r|-t] [-n LEVEL] [-s SIZE] [-w]

    Print or control the kernel ring buffer.

    -C	Clear ring buffer without printing
    -c	Clear ring buffer after printing
    -n	Set kernel logging LEVEL (1-9)
    -r	Raw output (with <level markers>)
    -s	Show the last SIZE many bytes
    -t	Don't print kernel's timestamps
    -w	Keep waiting for more output (aka --follow)
*/

#define FOR_dmesg
#include "toys.h"
#include <sys/klog.h>

GLOBALS(
  long level;
  long size;

  int color;
)

static int xklogctl(int type, char *buf, int len)
{
  int rc = klogctl(type, buf, len);

  if (rc<0) perror_exit("klogctl");

  return rc;
}

// Use klogctl for reading if we're on a pre-3.5 kernel.
static void legacy_mode()
{
  char *data, *to, *from;
  int size;

  // Figure out how much data we need, and fetch it.
  if (!(size = TT.size)) size = xklogctl(10, 0, 0);
  data = to = from = xmalloc(size+1);
  data[size = xklogctl(3 + (toys.optflags & FLAG_c), data, size)] = 0;

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

static void color(int c)
{
  if (TT.color) printf("\033[%dm", c);
}

static void print_all(void)
{
  // http://kernel.org/doc/Documentation/ABI/testing/dev-kmsg

  // Each read returns one message. By default, we block when there are no
  // more messages (--follow); O_NONBLOCK is needed for for usual behavior.
  int fd = xopen("/dev/kmsg", O_RDONLY | ((toys.optflags&FLAG_w)?0:O_NONBLOCK));

  // With /dev/kmsg, SYSLOG_ACTION_CLEAR (5) doesn't actually remove anything;
  // you need to seek to the last clear point.
  lseek(fd, 0, SEEK_DATA);

  while (1) {
    char msg[8192]; // CONSOLE_EXT_LOG_MAX.
    unsigned long long time_us;
    int facpri, subsystem, pos;
    char *p, *text;
    ssize_t len;

    // kmsg fails with EPIPE if we try to read while the buffer moves under
    // us; the next read will succeed and return the next available entry.
    do {
      len = read(fd, msg, sizeof(msg));
    } while (len == -1 && errno == EPIPE);
    // All reads from kmsg fail if you're on a pre-3.5 kernel.
    if (len == -1 && errno == EINVAL) {
      close(fd);
      return legacy_mode();
    }
    if (len <= 0) break;

    msg[len] = 0;

    if (sscanf(msg, "%u,%*u,%llu,%*[^;];%n", &facpri, &time_us, &pos) != 2)
      continue;

    // Drop extras after end of message text.
    text = msg + pos;
    if ((p = strchr(text, '\n'))) *p = 0;

    // Is there a subsystem? (The ": " is just a convention.)
    p = strstr(text, ": ");
    subsystem = p ? (p - text) : 0;

    // "Raw" is a lie for /dev/kmsg. In practice, it just means we show the
    // syslog facility/priority at the start of each line.
    if (toys.optflags&FLAG_r) printf("<%d>", facpri);

    if (!(toys.optflags&FLAG_t)) {
      color(32);
      printf("[%5lld.%06lld] ", time_us/1000000, time_us%1000000);
      color(0);
    }

    // Errors (or worse) are shown in red, subsystems are shown in yellow.
    if (subsystem) {
      color(33);
      printf("%.*s", subsystem, text);
      text += subsystem;
      color(0);
    }
    if (!((facpri&7) <= 3)) xputs(text);
    else {
      color(31);
      printf("%s", text);
      color(0);
      xputc('\n');
    }
  }
  close(fd);
}

void dmesg_main(void)
{
  TT.color = isatty(1);

  if (!(toys.optflags & (FLAG_C|FLAG_n))) print_all();

  // Set the log level?
  if (toys.optflags & FLAG_n) xklogctl(8, 0, TT.level);

  // Clear the buffer?
  if (toys.optflags & (FLAG_C|FLAG_c)) xklogctl(5, 0, 0);
}
