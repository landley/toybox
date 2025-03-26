/* log.c - Log to logcat.
 *
 * Copyright 2016 The Android Open Source Project

USE_LOG(NEWTOY(log, "b:p:t:", TOYFLAG_USR|TOYFLAG_SBIN))

config LOG
  bool "log"
  depends on TOYBOX_ON_ANDROID
  default y
  help
    usage: log [-b BUFFER] [-p PRI] [-t TAG] [MESSAGE...]

    Logs message (or stdin) to logcat.

    -b	Use the given log buffer instead of "main":
    	"radio", "events", "system", "crash", "stats", "security", "kernel"
    -p	Use the given priority instead of INFO:
    	d: DEBUG  e: ERROR  f: FATAL  i: INFO  v: VERBOSE  w: WARN  s: SILENT
    -t	Use the given tag instead of "log"
*/

#define FOR_log
#include "toys.h"

GLOBALS(
  char *t, *p, *b;

  int pri, buf;
)

static void log_line(char **pline, long len)
{
  if (!pline) return;
  __android_log_buf_write(TT.buf, TT.pri, TT.t, *pline);
}

void log_main(void)
{
  char *s = toybuf;
  int i;

  TT.pri = ANDROID_LOG_INFO;
  if (TT.p) {
    i = stridx("defisvw", tolower(*TT.p));
    if (i==-1 || strlen(TT.p)!=1) error_exit("bad -p '%s'", TT.p);
    TT.pri = (int[]) {ANDROID_LOG_DEBUG, ANDROID_LOG_ERROR,
      ANDROID_LOG_FATAL, ANDROID_LOG_INFO, ANDROID_LOG_SILENT,
      ANDROID_LOG_VERBOSE, ANDROID_LOG_WARN}[i];
  }
  if (!TT.t) TT.t = "log";
  if (TT.b) TT.buf = anystr(TT.b, (char *[]){"main", "radio", "events",
    "system", "crash", "stats", "security", "kernel", 0})-1;
  if (TT.buf<0) error_exit("unknown log buffer: %s", TT.b);

  if (toys.optc) {
    for (i = 0; toys.optargs[i]; i++) {
      if (i) *s++ = ' ';
      if ((s-toybuf)+strlen(toys.optargs[i])>=1024) {
        memcpy(s, toys.optargs[i], 1024-(s-toybuf));
        toybuf[1024] = 0;
        error_msg("log cut at 1024 bytes");
        break;
      }
      s = stpcpy(s, toys.optargs[i]);
    }
  } else do_lines(0, '\n', log_line);

  __android_log_buf_write(TT.buf, TT.pri, TT.t, toybuf);
}
