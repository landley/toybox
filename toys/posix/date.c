/* date.c - set/get the date
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/date.html
 *
 * Note: setting a 2 year date is 50 years back/forward from today,
 * not posix's hardwired magic dates.

USE_DATE(NEWTOY(date, "r:u", TOYFLAG_BIN))

config DATE
  bool "date"
  default y
  help
    usage: date [-u] [-r FILE] [+FORMAT] | mmddhhmm[[cc]yy[.ss]]

    Set/get the current date/time.

    Setting the date requires month, day, hour (0-23), and minute, each
    two digits. It can optionally include year, century, and .seconds.

    -u	Use UTC timezone instead of current
    -r	Use date from FILE instead of current date
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
    if (!strftime(toybuf, sizeof(toybuf), format_string, &tm)) goto bad_format;

    puts(toybuf);

  // Set the date
  } else {
    struct timeval tv;
    char *s = *toys.optargs;
    int len;

    // Date format: mmddhhmm[[cc]yy]
    len = 0;
    sscanf(s, "%2u%2u%2u%2u%n", &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
      &tm.tm_min, &len);
    if (len != 8) goto bad_date;
    s += len;
    tm.tm_mon--;

    // If year specified, overwrite one we fetched earlier
    if (*s && *s != '.') {
      unsigned year, r1 = tm.tm_year % 100, r2 = (tm.tm_year + 50) % 100,
        century = tm.tm_year - r1;

      len = 0;
      sscanf(s, "%u%n", &year, &len);
      if (len == 4) year -= 1900;
      else if (len != 2) goto bad_date;
      s += len;

      // 2 digit years, next 50 years are "future", last 50 years are "past".
      // A "future" date in past is a century ahead.
      // A non-future date in the future is a century behind.
      if ((r1 < r2) ? (r1 < year && year < r2) : (year < r1 || year > r2)) {
        if (year < r1) year += 100;
      } else if (year > r1) year -= 100;
      tm.tm_year = year + century;
    }
    if (*s == '.') {
      len = 0;
      sscanf(s, ".%u%n", &tm.tm_sec, &len);
      s += len;
    }
    if (*s) goto bad_date;

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
    if (tv.tv_sec == (time_t)-1) goto bad_date;

    tv.tv_usec = 0;
    if (!strftime(toybuf, sizeof(toybuf), format_string, &tm)) goto bad_format;
    puts(toybuf);
    if (settimeofday(&tv, NULL) < 0) perror_msg("cannot set date");
  }

  return;

bad_date:
  error_exit("bad date '%s'", *toys.optargs);
bad_format:
  perror_msg("bad format '%s'", format_string);
}
