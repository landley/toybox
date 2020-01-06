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

    %F "%Y-%m-%d"     %R "%H:%M"        %T "%H:%M:%S"    %z numeric timezone
    %D "%m/%d/%y"     %r "%I:%M:%S %p"  %h "%b"          %s unix epoch time
    %x locale date    %X locale time    %c locale date/time
*/

#define FOR_date
#include "toys.h"

GLOBALS(
  char *r, *D, *d;

  unsigned nano;
)

// Handles any leading `TZ="blah" ` in the input string.
static void parse_date(char *str, time_t *t)
{
  char *new_tz = NULL, *old_tz, *s = str;

  if (!strncmp(str, "TZ=\"", 4)) {
    // Extract the time zone and skip any whitespace.
    new_tz = str+4;
    if (!(str = strchr(new_tz, '"'))) xvali_date(0, s);
    *str++ = 0;
    while (isspace(*str)) str++;

    // Switch $TZ.
    old_tz = getenv("TZ");
    setenv("TZ", new_tz, 1);
    tzset();
  }
  time(t);
  xparsedate(str, t, &TT.nano, 1);
  if (new_tz) {
    if (old_tz) setenv("TZ", old_tz, 1);
    else unsetenv("TZ");
  }
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

      t = (s && *s) ? xvali_date(&tm, s) : xvali_date(0, TT.d);
    } else parse_date(TT.d, &t);
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

    parse_date(setdate, &t);
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
}
