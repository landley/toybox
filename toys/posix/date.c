/* date.c - set/get the date
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/date.html

USE_DATE(NEWTOY(date, "r:u", TOYFLAG_BIN))

config DATE
  bool "date"
  default y
  help
    usage: date [-u] [-r file] [+format] | mmddhhmm[[cc]yy]

    Set/get the current date/time
*/

#define FOR_date
#include "toys.h"

GLOBALS(
  char *file;
)

void date_main(void)
{
  const char *format_string = "%a %b %e %H:%M:%S %Z %Y";
  time_t now = time(NULL);
  struct tm tm;

  if (TT.file) {
    struct stat st;

    xstat(TT.file, &st);
    now = st.st_mtim.tv_sec;
  }
  ((toys.optflags & FLAG_u) ? gmtime_r : localtime_r)(&now, &tm);

  // Display the date?
  if (!toys.optargs[0] || toys.optargs[0][0] == '+') {
    if (toys.optargs[0]) format_string = toys.optargs[0]+1;
    if (!strftime(toybuf, sizeof(toybuf), format_string, &tm))
      perror_msg("bad format `%s'", format_string);

    puts(toybuf);

  // Set the date
  } else {
    struct timeval tv;
    char *s = *toys.optargs;
    int len = strlen(s);

    if (len < 8 || len > 12 || (len & 1)) error_msg("bad date `%s'", s);

    // Date format: mmddhhmm[[cc]yy]
    memset(&tm, 0, sizeof(tm));
    len = sscanf(s, "%2u%2u%2u%2u", &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
      &tm.tm_min);
    tm.tm_mon--;

    // If year specified, overwrite one we fetched earlier
    if (len > 8) {
      sscanf(s, "%u", &tm.tm_year);
      if (len == 12) tm.tm_year -= 1900;
      /* 69-99 = 1969-1999, 0 - 68 = 2000-2068 */
      else if (tm.tm_year < 69) tm.tm_year += 100;
    }

    if (toys.optflags & FLAG_u) {
      // Get the UTC version of a struct tm
      char *tz = CFG_TOYBOX_FREE ? getenv("TZ") : 0;
      setenv("TZ", "UTC", 1);
      tzset();
      tv.tv_sec = mktime(&tm);
      if (CFG_TOYBOX_FREE) {
        if (tz) setenv("TZ", tz, 1);
        else unsetenv("TZ");
        tzset();
      }
    } else tv.tv_sec = mktime(&tm);

    if (tv.tv_sec == (time_t)-1) error_msg("bad `%s'", toys.optargs[0]);
    tv.tv_usec = 0;
    if (!strftime(toybuf, sizeof(toybuf), format_string, &tm))
      perror_msg("bad format `%s'", format_string);
    puts(toybuf);
    if (settimeofday(&tv, NULL) < 0) perror_msg("cannot set date");
  }
}
