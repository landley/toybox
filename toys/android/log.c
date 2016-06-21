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
    		d: DEBUG  e: ERROR    f: FATAL
    		i: INFO   v: VERBOSE  w: WARN
    -t	use the given tag instead of "log"
*/

#define FOR_log
#include "toys.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

GLOBALS(
  char *tag;
  char *pri;
)

void log_main(void)
{
#if defined(__ANDROID__)
  android_LogPriority pri = ANDROID_LOG_INFO;
  int i;

  if (TT.pri) {
    if (strlen(TT.pri) != 1) TT.pri = "?";
    switch (tolower(*TT.pri)) {
    case 'd': pri = ANDROID_LOG_DEBUG; break;
    case 'e': pri = ANDROID_LOG_ERROR; break;
    case 'f': pri = ANDROID_LOG_FATAL; break;
    case 'i': pri = ANDROID_LOG_INFO; break;
    case 's': pri = ANDROID_LOG_SILENT; break;
    case 'v': pri = ANDROID_LOG_VERBOSE; break;
    case 'w': pri = ANDROID_LOG_WARN; break;
    case '*': pri = ANDROID_LOG_DEFAULT; break;
    default: error_exit("bad -p '%s'", TT.pri);
    }
  }
  if (!TT.tag) TT.tag = "log";

  for (i = 0; toys.optargs[i]; i++) {
    if (i > 0) xstrncat(toybuf, " ", sizeof(toybuf));
    xstrncat(toybuf, toys.optargs[i], sizeof(toybuf));
  }

  __android_log_write(pri, TT.tag, toybuf);
#endif
}
