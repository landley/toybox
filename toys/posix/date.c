/* date.c - set/get the date
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/date.html
 *
 * Note: setting a 2 year date is 50 years back/forward from today,
 * not posix's hardwired magic dates.

USE_DATE(NEWTOY(date, "d:D:r:u[!dr]", TOYFLAG_BIN))

config DATE
  bool "date"
  default y
  help
    usage: date [-u] [-r FILE] [-d DATE] [+DISPLAY_FORMAT] [-D SET_FORMAT] [SET]

    Set/get the current date/time. With no SET shows the current date.

    Default SET format is "MMDDhhmm[[CC]YY][.ss]", that's (2 digits each)
    month, day, hour (0-23), and minute. Optionally century, year, and second.
    Also accepts "@UNIXTIME[.FRACTION]" as seconds since midnight Jan 1 1970.

    -d	Show DATE instead of current time (convert date format)
    -D	+FORMAT for SET or -d (instead of MMDDhhmm[[CC]YY][.ss])
    -r	Use modification time of FILE instead of current date
    -u	Use UTC instead of current timezone

    +FORMAT specifies display format string using strftime(3) syntax:

    %% literal %             %n newline              %t tab
    %S seconds (00-60)       %M minute (00-59)       %m month (01-12)
    %H hour (0-23)           %I hour (01-12)         %p AM/PM
    %y short year (00-99)    %Y year                 %C century
    %a short weekday name    %A weekday name         %u day of week (1-7, 1=mon)
    %b short month name      %B month name           %Z timezone name
    %j day of year (001-366) %d day of month (01-31) %e day of month ( 1-31)
    %N nanosec (output only)

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

  unsigned nano;
)

// Handle default posix date format (mmddhhmm[[cc]yy]) or @UNIX[.FRAC]
// returns 0 success, nonzero for error
static int parse_default(char *str, struct tm *tm)
{
  int len = 0;

  // Parse @UNIXTIME[.FRACTION]
  if (*str == '@') {
    long long ll;
    time_t tt;

    // Collect seconds and nanoseconds
    // Note: struct tm hasn't got a fractional seconds field, thus strptime()
    // doesn't support it, so store nanoseconds out of band (in globals).
    // tt and ll are separate because we can't guarantee time_t is 64 bit (yet).
    sscanf(str, "@%lld%n", &ll, &len);
    if (str[len]=='.') {
      str += len+1;
      for (len = 0; len<9; len++) {
        TT.nano *= 10;
        if (isdigit(str[len])) TT.nano += str[len]-'0';
      }
    }
    if (str[len]) return 1;
    tt = ll;
    gmtime_r(&tt, tm);

    return 0;
  }

  // Posix format
  sscanf(str, "%2u%2u%2u%2u%n", &tm->tm_mon, &tm->tm_mday, &tm->tm_hour,
    &tm->tm_min, &len);
  if (len != 8) return 1;
  str += len;
  tm->tm_mon--;

  // If year specified, overwrite one we fetched earlier
  if (*str && *str != '.') {
    unsigned year;

    len = 0;
    sscanf(str, "%u%n", &year, &len);
    if (len == 4) tm->tm_year = year - 1900;
    else if (len != 2) return 1;
    str += len;

    // 2 digit years, next 50 years are "future", last 50 years are "past".
    // A "future" date in past is a century ahead.
    // A non-future date in the future is a century behind.
    if (len == 2) {
      unsigned r1 = tm->tm_year % 100, r2 = (tm->tm_year + 50) % 100,
        century = tm->tm_year - r1;

      if ((r1 < r2) ? (r1 < year && year < r2) : (year < r1 || year > r2)) {
        if (year < r1) year += 100;
      } else if (year > r1) year -= 100;
      tm->tm_year = year + century;
    }
  }
  if (*str == '.') {
    len = 0;
    sscanf(str, ".%u%n", &tm->tm_sec, &len);
    str += len;
  } else tm->tm_sec = 0;

  return *str;
}

static void check_range(int a, int low, int high)
{
  if (a<low) error_exit("%d<%d", a, low);
  if (a>high) error_exit("%d>%d", a, high);
}

// Print strftime plus %N escape(s). note: modifies fmt for %N
static void puts_time(char *fmt, struct tm *tm)
{
  char *s, *snap;
  long width = width;

  for (s = fmt;;s++) {

    // Find next %N or end
    if (*(snap = s) == '%') {
      width = isdigit(*++s) ? *(s++)-'0' : 9;
      if (*s && *s != 'N') continue;
    } else if (*s) continue;

    // Don't modify input string if no %N (default format is constant string).
    if (*s) *snap = 0;
    if (!strftime(toybuf, sizeof(toybuf)-10, fmt, tm))
      perror_exit("bad format '%s'", fmt);
    if (*s) {
      snap = toybuf+strlen(toybuf);
      sprintf(snap, "%09u", TT.nano);
      snap[width] = 0;
    }
    fputs(toybuf, stdout);
    if (!*s || !*(fmt = s+1)) break;
  }
  xputc('\n');
}

void date_main(void)
{
  char *setdate = *toys.optargs, *format_string = "%a %b %e %H:%M:%S %Z %Y";
  struct tm tm;

  memset(&tm, 0, sizeof(struct tm));

  if (TT.showdate) {
    if (TT.setfmt) {
      char *s = strptime(TT.showdate, TT.setfmt+(*TT.setfmt=='+'), &tm);

      if (!s || *s) goto bad_showdate;
    } else if (parse_default(TT.showdate, &tm)) goto bad_showdate;
  } else {
    struct timespec ts;
    struct stat st;

    if (TT.file) {
      xstat(TT.file, &st);
      ts = st.st_mtim;
    } else clock_gettime(CLOCK_REALTIME, &ts);

    ((toys.optflags & FLAG_u) ? gmtime_r : localtime_r)(&ts.tv_sec, &tm);
    TT.nano = ts.tv_nsec;
  }

  // Fall through if no arguments
  if (!setdate);
  // Display the date?
  else if (*setdate == '+') {
    format_string = toys.optargs[0]+1;
    setdate = toys.optargs[1];

  // Set the date
  } else if (setdate) {
    char *tz;
    struct timeval tv;
    int u = toys.optflags & FLAG_u;

    if (parse_default(setdate, &tm)) goto bad_setdate;

    check_range(tm.tm_sec, 0, 60);
    check_range(tm.tm_min, 0, 59);
    check_range(tm.tm_hour, 0, 23);
    check_range(tm.tm_mday, 1, 31);
    check_range(tm.tm_mon, 0, 11);

    if (u) {
      tz = getenv("TZ");
      setenv("TZ", "UTC", 1);
      tzset();
    }
    errno = 0;
    tv.tv_sec = mktime(&tm);
    if (errno) goto bad_setdate;
    if (u) {
      if (tz) setenv("TZ", tz, 1);
      else unsetenv("TZ");
      tzset();
    }

    tv.tv_usec = TT.nano/1000;
    if (settimeofday(&tv, NULL) < 0) perror_msg("cannot set date");
  }

  puts_time(format_string, &tm);
  return;

bad_showdate:
  setdate = TT.showdate;
bad_setdate:
  perror_exit("bad date '%s'", setdate);
}
