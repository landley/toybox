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

    -d	Show DATE instead of current time (convert date format)
    -D	+FORMAT for SET or -d (instead of MMDDhhmm[[CC]YY][.ss])
    -r	Use modification time of FILE instead of current date
    -u	Use UTC instead of current timezone

    Supported input formats:

    MMDDhhmm[[CC]YY][.ss]     POSIX
    @UNIXTIME[.FRACTION]      seconds since midnight 1970-01-01
    YYYY-MM-DD [hh:mm[:ss]]   ISO 8601
    hh:mm[:ss]                24-hour time today

    All input formats can be preceded by TZ="id" to set the input time zone
    separately from the output time zone. Otherwise $TZ sets both.

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
  char *r, *D, *d;

  unsigned nano;
)

static void check_range(int a, int low, int high)
{
  if (a<low) error_exit("%d<%d", a, low);
  if (a>high) error_exit("%d>%d", a, high);
}

static void check_tm(struct tm *tm)
{
  check_range(tm->tm_sec, 0, 60);
  check_range(tm->tm_min, 0, 59);
  check_range(tm->tm_hour, 0, 23);
  check_range(tm->tm_mday, 1, 31);
  check_range(tm->tm_mon, 0, 11);
}

// Returns 0 success, nonzero for error.
static int parse_formats(char *str, time_t *t)
{
  struct tm tm;
  time_t now;
  int len = 0, i;
  char *formats[] = {
    // Formats with years must come first.
    "%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d",
    "%H:%M:%S", "%H:%M"
  };

  // Parse @UNIXTIME[.FRACTION]
  if (*str == '@') {
    long long ll;

    // Collect seconds and nanoseconds.
    // &ll is not just t because we can't guarantee time_t is 64 bit (yet).
    sscanf(str, "@%lld%n", &ll, &len);
    if (str[len]=='.') {
      str += len+1;
      for (len = 0; len<9; len++) {
        TT.nano *= 10;
        if (isdigit(str[len])) TT.nano += str[len]-'0';
      }
    }
    if (str[len]) return 1;
    *t = ll;
    return 0;
  }

  // Is it one of the fancy formats?
  for (i = 0; i<ARRAY_LEN(formats); i++) {
    char *p;

    now = time(0);
    localtime_r(&now, &tm);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    if ((p = strptime(str,formats[i],&tm)) && !*p) {
      if ((*t = mktime(&tm)) != -1) return 0;
    }
  }

  // Posix format?
  sscanf(str, "%2u%2u%2u%2u%n", &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
    &tm.tm_min, &len);
  if (len != 8) return 1;
  str += len;
  tm.tm_mon--;

  // If year specified, overwrite one we fetched earlier.
  if (*str && *str != '.') {
    unsigned year;

    len = 0;
    sscanf(str, "%u%n", &year, &len);
    if (len == 4) tm.tm_year = year - 1900;
    else if (len != 2) return 1;
    str += len;

    // 2 digit years, next 50 years are "future", last 50 years are "past".
    // A "future" date in past is a century ahead.
    // A non-future date in the future is a century behind.
    if (len == 2) {
      unsigned r1 = tm.tm_year % 100, r2 = (tm.tm_year + 50) % 100,
        century = tm.tm_year - r1;

      if ((r1 < r2) ? (r1 < year && year < r2) : (year < r1 || year > r2)) {
        if (year < r1) year += 100;
      } else if (year > r1) year -= 100;
      tm.tm_year = year + century;
    }
  }
  // Fractional part?
  if (*str == '.') {
    len = 0;
    sscanf(str, ".%u%n", &tm.tm_sec, &len);
    str += len;
  } else tm.tm_sec = 0;

  // Does that look like a valid date?
  check_tm(&tm);
  if ((*t = mktime(&tm)) == -1) return 1;

  // Shouldn't be any trailing garbage.
  return *str;
}

// Handles any leading `TZ="blah" ` in the input string.
static int parse_date(char *str, time_t *t)
{
  char *new_tz = NULL, *old_tz;
  int result;

  if (!strncmp(str, "TZ=\"", 4)) {
    // Extract the time zone and skip any whitespace.
    new_tz = str+4;
    str = strchr(new_tz, '"');
    if (!str) return 1;
    *str++ = '\0';
    while (isspace(*str)) ++str;

    // Switch $TZ.
    old_tz = getenv("TZ");
    setenv("TZ", new_tz, 1);
    tzset();
  }
  result = parse_formats(str, t);
  if (new_tz) {
    if (old_tz) setenv("TZ", old_tz, 1);
    else unsetenv("TZ");
  }
  return result;
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
  char *setdate = *toys.optargs, *format_string = "%a %b %e %H:%M:%S %Z %Y",
    *tz = NULL;
  time_t t;

  if (FLAG(u)) {
    tz = getenv("TZ");
    setenv("TZ", "UTC", 1);
    tzset();
  }

  if (TT.d) {
    if (TT.D) {
      struct tm tm = {};
      char *s = strptime(TT.d, TT.D+(*TT.D=='+'), &tm);

      if (!s || *s) goto bad_showdate;
      check_tm(&tm);
      if ((t = mktime(&tm)) == -1) goto bad_showdate;
    } else if (parse_date(TT.d, &t)) goto bad_showdate;
  } else {
    struct timespec ts;
    struct stat st;

    if (TT.r) {
      xstat(TT.r, &st);
      ts = st.st_mtim;
    } else clock_gettime(CLOCK_REALTIME, &ts);

    t = ts.tv_sec;
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
    struct timeval tv;

    if (parse_date(setdate, &t)) goto bad_setdate;
    tv.tv_sec = t;
    tv.tv_usec = TT.nano/1000;
    if (settimeofday(&tv, NULL) < 0) perror_msg("cannot set date");
  }

  puts_time(format_string, localtime(&t));

  if (FLAG(u)) {
    if (tz) setenv("TZ", tz, 1);
    else unsetenv("TZ");
    tzset();
  }

  return;

bad_showdate:
  setdate = TT.d;
bad_setdate:
  error_exit("bad date '%s'", setdate);
}
