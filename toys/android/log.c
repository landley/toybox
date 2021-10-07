/* log.c - Log to logcat.
 *
 * Copyright 2016 The Android Open Source Project

USE_LOG(NEWTOY(log, "p:t:", TOYFLAG_USR|TOYFLAG_SBIN))

config LOG
  bool "log"
  depends on TOYBOX_ON_ANDROID
  default y
  help
    usage: log [-p PRI] [-t TAG] [MESSAGE...]

    Logs message (or stdin) to logcat.

    -p	Use the given priority instead of INFO:
    	d: DEBUG  e: ERROR  f: FATAL  i: INFO  v: VERBOSE  w: WARN  s: SILENT
    -t	Use the given tag instead of "log"
*/

#define FOR_log
#include "toys.h"

GLOBALS(
  char *t, *p;
)

void log_main(void)
{
  android_LogPriority pri = ANDROID_LOG_INFO;
  char *s = toybuf;
  int i;

  if (TT.p) {
    i = stridx("defisvw", tolower(*TT.p));
    if (i==-1 || strlen(TT.p)!=1) error_exit("bad -p '%s'", TT.p);
    pri = (android_LogPriority []){ANDROID_LOG_DEBUG, ANDROID_LOG_ERROR,
      ANDROID_LOG_FATAL, ANDROID_LOG_INFO, ANDROID_LOG_SILENT,
      ANDROID_LOG_VERBOSE, ANDROID_LOG_WARN}[i];
  }
  if (!TT.t) TT.t = "log";

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
  } else toybuf[readall(0, toybuf, 1024-1)] = 0;

  __android_log_write(pri, TT.t, toybuf);
}
