/* dmesg.c - display/control kernel ring buffer.
 *
 * Copyright 2006, 2007 Rob Landley <rob@landley.net>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/dmesg.html

// We care that FLAG_c is 1, so keep c at the end.
USE_DMESG(NEWTOY(dmesg, "w(follow)CSTtrs#<1n#c[!Ttr][!Cc]", TOYFLAG_BIN))

config DMESG
  bool "dmesg"
  default n
  help
    usage: dmesg [-Cc] [-r|-t|-T] [-n LEVEL] [-s SIZE] [-w]

    Print or control the kernel ring buffer.

    -C	Clear ring buffer without printing
    -c	Clear ring buffer after printing
    -n	Set kernel logging LEVEL (1-9)
    -r	Raw output (with <level markers>)
    -S	Use syslog(2) rather than /dev/kmsg
    -s	Show the last SIZE many bytes
    -T	Show human-readable timestamps
    -t	Don't print timestamps
    -w	Keep waiting for more output (aka --follow)
*/

#define FOR_dmesg
#include "toys.h"
#include <sys/klog.h>

GLOBALS(
  long level;
  long size;

  int use_color;
  struct sysinfo info;
)

static void color(int c)
{
  if (TT.use_color) printf("\033[%dm", c);
}

static void format_message(char *msg, int new) {
  unsigned long long time_s;
  unsigned long long time_us;
  int facpri, subsystem, pos;
  char *p, *text;

  // The new /dev/kmsg and the old syslog(2) formats differ slightly.
  if (new) {
    if (sscanf(msg, "%u,%*u,%llu,%*[^;];%n", &facpri, &time_us, &pos) != 2)
      return;

    time_s = time_us/1000000;
    time_us %= 1000000;
  } else {
    if (sscanf(msg, "<%u>[%llu.%llu] %n",
               &facpri, &time_s, &time_us, &pos) != 3)
      return;
  }

  // Drop extras after end of message text.
  text = msg + pos;
  if ((p = strchr(text, '\n'))) *p = 0;

  // Is there a subsystem? (The ": " is just a convention.)
  p = strstr(text, ": ");
  subsystem = p ? (p - text) : 0;

  // "Raw" is a lie for /dev/kmsg. In practice, it just means we show the
  // syslog facility/priority at the start of each line to emulate the
  // historical syslog(2) format.
  if (toys.optflags&FLAG_r) printf("<%d>", facpri);

  // Format the time.
  if (!(toys.optflags&FLAG_t)) {
    color(32);
    if (toys.optflags&FLAG_T) {
      time_t t = (time(NULL) - TT.info.uptime) + time_s;
      char *ts = ctime(&t);

      printf("[%.*s] ", (int)(strlen(ts) - 1), ts);
    } else {
      printf("[%5lld.%06lld] ", time_s, time_us);
    }
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

  // Break into messages (one per line) and send each one to format_message.
  to = data + size;
  while (from < to) {
    char *msg_end = memchr(from, '\n', (to-from));

    if (!msg_end) break;
    *msg_end = '\0';
    format_message(from, 0);
    from = msg_end + 1;
  }

  if (CFG_TOYBOX_FREE) free(data);
}

static void print_all(void)
{
  if (toys.optflags&FLAG_T) sysinfo(&TT.info);
  if (toys.optflags&FLAG_S) return legacy_mode();

  // http://kernel.org/doc/Documentation/ABI/testing/dev-kmsg

  // Each read returns one message. By default, we block when there are no
  // more messages (--follow); O_NONBLOCK is needed for for usual behavior.
  int fd = xopen("/dev/kmsg", O_RDONLY | ((toys.optflags&FLAG_w)?0:O_NONBLOCK));

  // With /dev/kmsg, SYSLOG_ACTION_CLEAR (5) doesn't actually remove anything;
  // you need to seek to the last clear point.
  lseek(fd, 0, SEEK_DATA);

  while (1) {
    char msg[8192]; // CONSOLE_EXT_LOG_MAX.
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
    format_message(msg, 1);
  }
  close(fd);
}

void dmesg_main(void)
{
  TT.use_color = isatty(1);

  if (!(toys.optflags & (FLAG_C|FLAG_n))) print_all();

  // Set the log level?
  if (toys.optflags & FLAG_n) xklogctl(8, 0, TT.level);

  // Clear the buffer?
  if (toys.optflags & (FLAG_C|FLAG_c)) xklogctl(5, 0, 0);
}
