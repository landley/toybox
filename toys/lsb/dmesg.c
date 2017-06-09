/* dmesg.c - display/control kernel ring buffer.
 *
 * Copyright 2006, 2007 Rob Landley <rob@landley.net>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/dmesg.html
 *
 * Don't ask me why the horrible new dmesg API is still in "testing":
 * http://kernel.org/doc/Documentation/ABI/testing/dev-kmsg

// We care that FLAG_c is 1, so keep c at the end.
USE_DMESG(NEWTOY(dmesg, "w(follow)CSTtrs#<1n#c[!Ttr][!Cc][!Sw]", TOYFLAG_BIN))

config DMESG
  bool "dmesg"
  default y
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
  time_t tea;
)

static void color(int c)
{
  if (TT.use_color) printf("\033[%dm", c);
}

static void format_message(char *msg, int new)
{
  unsigned long long time_s, time_us;
  int facpri, subsystem, pos;
  char *p, *text;

  // The new /dev/kmsg and the old syslog(2) formats differ slightly.
  if (new) {
    if (sscanf(msg, "%u,%*u,%llu,%*[^;]; %n", &facpri, &time_us, &pos) != 2)
      return;

    time_s = time_us/1000000;
    time_us %= 1000000;
  } else if (sscanf(msg, "<%u>[%llu.%llu] %n",
                    &facpri, &time_s, &time_us, &pos) != 3) return;

  // Drop extras after end of message text.
  if ((p = strchr(text = msg+pos, '\n'))) *p = 0;

  // Is there a subsystem? (The ": " is just a convention.)
  p = strstr(text, ": ");
  subsystem = p ? (p-text) : 0;

  // To get "raw" output for /dev/kmsg we need to add priority to each line
  if (toys.optflags&FLAG_r) {
    color(0);
    printf("<%d>", facpri);
  }

  // Format the time.
  if (!(toys.optflags&FLAG_t)) {
    color(32);
    if (toys.optflags&FLAG_T) {
      time_t t = TT.tea+time_s;
      char *ts = ctime(&t);

      printf("[%.*s] ", (int)(strlen(ts)-1), ts);
    } else printf("[%5lld.%06lld] ", time_s, time_us);
  }

  // Errors (or worse) are shown in red, subsystems are shown in yellow.
  if (subsystem) {
    color(33);
    printf("%.*s", subsystem, text);
    text += subsystem;
  }
  color(31*((facpri&7)<=3));
  xputs(text);
}

static int xklogctl(int type, char *buf, int len)
{
  int rc = klogctl(type, buf, len);

  if (rc<0) perror_exit("klogctl");

  return rc;
}

static void dmesg_cleanup(void)
{
  color(0);
}

void dmesg_main(void)
{
  TT.use_color = isatty(1);

  if (TT.use_color) sigatexit(dmesg_cleanup);
  // If we're displaying output, is it klogctl or /dev/kmsg?
  if (toys.optflags & (FLAG_C|FLAG_n)) goto no_output;

  if (toys.optflags&FLAG_T) {
    struct sysinfo info;

    sysinfo(&info);
    TT.tea = time(0)-info.uptime;
  }

  if (!(toys.optflags&FLAG_S)) {
    char msg[8193]; // CONSOLE_EXT_LOG_MAX+1
    ssize_t len;
    int fd;

    // Each read returns one message. By default, we block when there are no
    // more messages (--follow); O_NONBLOCK is needed for for usual behavior.
    fd = open("/dev/kmsg", O_RDONLY|(O_NONBLOCK*!(toys.optflags&FLAG_w)));
    if (fd == -1) goto klogctl_mode;

    // SYSLOG_ACTION_CLEAR(5) doesn't actually remove anything from /dev/kmsg,
    // you need to seek to the last clear point.
    lseek(fd, 0, SEEK_DATA);

    for (;;) {
      // why does /dev/kmesg return EPIPE instead of EAGAIN if oldest message
      // expires as we read it?
      if (-1==(len = read(fd, msg, sizeof(msg))) && errno==EPIPE) continue;
      // read() from kmsg always fails on a pre-3.5 kernel.
      if (len==-1 && errno==EINVAL) goto klogctl_mode;
      if (len<1) break;

      msg[len] = 0;
      format_message(msg, 1);
    }
    close(fd);
  } else {
    char *data, *to, *from, *end;
    int size;

klogctl_mode:
    // Figure out how much data we need, and fetch it.
    if (!(size = TT.size)) size = xklogctl(10, 0, 0);
    data = from = xmalloc(size+1);
    data[size = xklogctl(3+(toys.optflags&FLAG_c), data, size)] = 0;

    // Send each line to format_message.
    to = data + size;
    while (from < to) {
      if (!(end = memchr(from, '\n', to-from))) break;
      *end = 0;
      format_message(from, 0);
      from = end + 1;
    }

    if (CFG_TOYBOX_FREE) free(data);
  }

no_output:
  // Set the log level?
  if (toys.optflags & FLAG_n) xklogctl(8, 0, TT.level);

  // Clear the buffer?
  if (toys.optflags & (FLAG_C|FLAG_c)) xklogctl(5, 0, 0);
}
