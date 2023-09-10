/* ts.c - timestamp input lines
 *
 * Copyright 2023 Oliver Webb <aquahobbyist@proton.me>
 *
 * See https://linux.die.net/man/1/ts

USE_TS(NEWTOY(ts, "si", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_MAYFORK))

config TS
  bool "ts"
  default n
  help
    usage: ts [-is] [FORMAT]

    Add timestamps to each line in pipeline. Default format without options
    "%b %d %H:%M:%S", with "%H:%M:%S".

    -i	Incremental (since previous line)
    -s	Since start
*/

#define FOR_ts
#include "toys.h"

void ts_main(void)
{
  time_t starttime = time(0), curtime, tt;
  char *format = toys.optflags ? "%T" : "%b %d %T", *line;
  struct tm *submtime;

  if (toys.optargs[0]) format = *toys.optargs;

  while ((line = xgetline(stdin))) {
    tt = curtime = time(0);

    if (toys.optflags) {
      curtime -= starttime;
      submtime = gmtime(&curtime);
    } else submtime = localtime(&curtime);
    if (FLAG(i)) starttime = tt;
    strftime(toybuf, sizeof(toybuf)-1, format, submtime);
    xprintf("%s %s\n", toybuf, line);
  }
}
