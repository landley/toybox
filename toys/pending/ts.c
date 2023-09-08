/* ts.c - timestamp input lines
 *
 * Copyright 2023 Oliver Webb <aquahobbyist@proton.me>
 *
 * See https://linux.die.net/man/1/ts

USE_TS(NEWTOY(ts, "i", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_MAYFORK))

config TS
  bool "ts"
  default n
  help
    usage: ts [-i] [FORMAT]

	timestamp input using strftime(3) The default formatting being "%b %d %H:%M:%S"
	-i Incremental timestamps (Changes Default Formatting to "%H:%M:S") 
*/

#define FOR_ts
#include "toys.h"

static time_t starttime;
static int len;
static char *buffer, *format;

static char *getftime(void)
{
  // A easy way to do incremental times is pretend it's 1970 
  time_t curtime = time(NULL);
  struct tm *submtime;
  if (FLAG(i)) { 
	curtime -= starttime;
	submtime = gmtime(&curtime);
  } else submtime = localtime(&curtime); 
  strftime(buffer,len,format,submtime);
  return buffer;
}

void ts_main(void)
{
  starttime = time(NULL);
  format = "%b %d %T";
  if (FLAG(i)) format = "%T";
  if (toys.optargs[0]) format = toys.optargs[0];
  // The arbitrary malloc size is because (On English locales), the maximum 
  // length of a strftime sequence is 10 bytes (%F and %s). In the worst case every 
  // 2 bytes translate to 10, A expansion ratio of 5. The 16 byte padding is to 
  // account for locales with names that exceed 10 bytes. 
  len = (strlen(format)*5)+16; 
  buffer = xmalloc(len);

  char *line;
  while ((line = xgetline(stdin))) xprintf("%s %s\n",getftime(),line);
}
