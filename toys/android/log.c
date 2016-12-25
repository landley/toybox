/* log.c - Log to logcat.
 *
 * Copyright 2016 The Android Open Source Project

USE_LOG(NEWTOY(log, "<1p:t:", TOYFLAG_USR|TOYFLAG_SBIN))

config LOG
  bool "log"
  depends on TOYBOX_ON_ANDROID
  default y
  help
    usage: log [-p PRI] [-t TAG] MESSAGE...

    Logs message to logcat.

    -p	use the given priority instead of INFO:
    	d: DEBUG  e: ERROR  f: FATAL  i: INFO  v: VERBOSE  w: WARN  s: SILENT
    -t	use the given tag instead of "log"
*/

#define FOR_log
#include "toys.h"
#include <android/log.h>

GLOBALS(
  char *tag;
  char *pri;
)

void log_main(void)
{
  android_LogPriority pri = ANDROID_LOG_INFO;
  char *s = toybuf;
  int i;

  if (TT.pri) {
    i = stridx("defisvw", tolower(*TT.pri));
    if (i==-1 || strlen(TT.pri)!=1) error_exit("bad -p '%s'", TT.pri);
    pri = (android_LogPriority []){ANDROID_LOG_DEBUG, ANDROID_LOG_ERROR,
      ANDROID_LOG_FATAL, ANDROID_LOG_INFO, ANDROID_LOG_SILENT,
      ANDROID_LOG_VERBOSE, ANDROID_LOG_WARN}[i];
  }
  if (!TT.tag) TT.tag = "log";

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

  __android_log_write(pri, TT.tag, toybuf);
}
