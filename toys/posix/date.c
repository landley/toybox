/* date.c - set/get the date
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/date.html
 *
 * Note: setting a 2 year date is 50 years back/forward from today,
 * not posix's hardwired magic dates.

USE_DATE(NEWTOY(date, "d:s:r:u[!dr]", TOYFLAG_BIN))

config DATE
  bool "date"
  default y
  help
    usage: date [-u] [-r FILE] [-d DATE] [+DISPLAY_FORMAT] [-s SET_FORMAT] [SET]

    Set/get the current date/time. With no SET shows the current date.

    Default SET format is "MMDDhhmm[[CC]YY][.ss]", that's (2 digits each)
    month, day, hour (0-23), and minute. Optionally century, year, and second.

    -d	Show DATE instead of current time (convert date format)
    -r	Use modification time of FILE instead of current date
    -s	+FORMAT for SET or -d (instead of MMDDhhmm[[CC]YY][.ss])
    -u	Use UTC instead of current timezone

    +FORMAT specifies display format string using these escapes:

    %% literal %             %n newline              %t tab
    %S seconds (00-60)       %M minute (00-59)       %m month (01-12)
    %H hour (0-23)           %I hour (01-12)         %p AM/PM
    %y short year (00-99)    %Y year                 %C century
    %a short weekday name    %A weekday name         %u day of week (1-7, 1=mon)
    %b short month name      %B month name           %Z timezone name
    %j day of year (001-366) %d day of month (01-31) %e day of month ( 1-31)

    %U Week of year (0-53 start sunday)   %W Week of year (0-53 start monday)
    %V Week of year (1-53 start monday, week < 4 days not part of this year) 

    %D = "%m/%d/%y"    %r = "%I : %M : %S %p"   %T = "%H:%M:%S"   %h = "%b"
    %x locale date     %X locale time           %c locale date/time
*/

#define FOR_date
#include "toys.h"

GLOBALS(
  char *file;
  char *setfmt;
  char *showdate;
)

// Handle default posix date format: mmddhhmm[[cc]yy]
// returns 0 success, nonzero for error
int parse_posixdate(char *str, struct tm *tm)
{
  int len;

  len = 0;
  sscanf(str, "%2u%2u%2u%2u%n", &tm->tm_mon, &tm->tm_mday, &tm->tm_hour,
    &tm->tm_min, &len);
  if (len != 8) return 1;
  str += len;
  tm->tm_mon--;

  // If year specified, overwrite one we fetched earlier
  if (*str && *str != '.') {
    unsigned year, r1 = tm->tm_year % 100, r2 = (tm->tm_year + 50) % 100,
      century = tm->tm_year - r1;

    len = 0;
    sscanf(str, "%u%n", &year, &len);
    if (len == 4) year -= 1900;
    else if (len != 2) return 1;
    str += len;

    // 2 digit years, next 50 years are "future", last 50 years are "past".
    // A "future" date in past is a century ahead.
    // A non-future date in the future is a century behind.
    if ((r1 < r2) ? (r1 < year && year < r2) : (year < r1 || year > r2)) {
      if (year < r1) year += 100;
    } else if (year > r1) year -= 100;
    tm->tm_year = year + century;
  }
  if (*str == '.') {
    len = 0;
    sscanf(str, ".%u%n", &tm->tm_sec, &len);
    str += len;
  }

  return *str;
}

void date_main(void)
{
  char *setdate = *toys.optargs, *format_string = "%a %b %e %H:%M:%S %Z %Y",
       *tz = 0;
  struct tm tm;

  // We can't just pass a timezone to mktime because posix.
  if (toys.optflags & FLAG_u) {
    if (CFG_TOYBOX_FREE) tz = getenv("TZ");
    setenv("TZ", "UTC", 1);
    tzset();
  }

  if (TT.showdate) {
    setdate = TT.showdate;
    if (TT.setfmt) {
      char *s = strptime(TT.showdate, TT.setfmt+(*TT.setfmt=='+'), &tm);

      if (!s || *s) goto bad_date;
    } else if (parse_posixdate(TT.showdate, &tm)) goto bad_date;
  } else {
    time_t now;

    if (TT.file) {
      struct stat st;

      xstat(TT.file, &st);
      now = st.st_mtim.tv_sec;
    } else now = time(0);

    ((toys.optflags & FLAG_u) ? gmtime_r : localtime_r)(&now, &tm);
  }

  setdate = *toys.optargs;
  // Fall through if no arguments
  if (!setdate);
  // Display the date?
  else if (*setdate == '+') {
    format_string = toys.optargs[0]+1;
    setdate = toys.optargs[1];

  // Set the date
  } else if (setdate) {
    struct timeval tv;

    if (parse_posixdate(setdate, &tm)) goto bad_date;

    if (toys.optflags & FLAG_u) {
      char *tz = CFG_TOYBOX_FREE ? getenv("TZ") : 0;

      // We can't just pass a timezone to mktime because posix.
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
    if (settimeofday(&tv, NULL) < 0) perror_msg("cannot set date");
  }

  if (toys.optflags & FLAG_u) {
    if (tz) setenv("TZ", tz, 1);
    else unsetenv("TZ");
    tzset();
  }

  if (!strftime(toybuf, sizeof(toybuf), format_string, &tm))
    perror_exit("bad format '%s'", format_string);
  puts(toybuf);

  return;

bad_date:
  error_exit("bad date '%s'", setdate);
}
