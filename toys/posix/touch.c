/* touch.c : change timestamp of a file
 *
 * Copyright 2012 Choubey Ji <warior.linux@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/touch.html

USE_TOUCH(NEWTOY(touch, "<1acd:mr:t:h[!dtr]", TOYFLAG_BIN))

config TOUCH
  bool "touch"
  default y
  help
    usage: touch [-amch] [-d DATE] [-t TIME] [-r FILE] FILE...

    Update the access and modification times of each FILE to the current time.

    -a	change access time
    -m	change modification time
    -c	don't create file
    -h	change symlink
    -d	set time to DATE (in YYYY-MM-DDThh:mm:SS[.frac][tz] format)
    -t	set time to TIME (in [[CC]YY]MMDDhhmm[.ss][frac] format)
    -r	set time same as reference FILE
*/

#define FOR_touch
#include "toys.h"

GLOBALS(
  char *time;
  char *file;
  char *date;
)

void touch_main(void)
{
  struct timespec ts[2];
  char **ss;
  int fd, i;

  // use current time if no -t or -d
  ts[0].tv_nsec = UTIME_NOW;
  if (toys.optflags & (FLAG_t|FLAG_d)) {
    char *s, *date, **format;
    struct tm tm;
    int len = 0;

    // Initialize default values for time fields
    ts->tv_sec = time(0);
    ts->tv_nsec = 0;

    // List of search types
    if (toys.optflags & FLAG_d) {
      format = (char *[]){"%Y-%m-%dT%T", "%Y-%m-%d %T", 0};
      date = TT.date;
    } else {
      format = (char *[]){"%m%d%H%M", "%y%m%d%H%M", "%C%y%m%d%H%M", 0};
      date = TT.time;
    }

    // Trailing Z means UTC timezone, don't expect libc to know this.
    i = strlen(s = date);
    if (i && toupper(date[i-1])=='Z') {
      date[i-1] = 0;
      setenv("TZ", "UTC0", 1);
    }

    while (*format) {
      if (toys.optflags&FLAG_t) {
        s = strchr(date, '.');
        if ((s ? s-date : strlen(date)) != strlen(*format)) {
          format++;
          continue;
        }
      }
      localtime_r(&(ts->tv_sec), &tm);
      // Adjusting for daylight savings time gives the wrong answer.
      tm.tm_isdst = 0;
      tm.tm_sec = 0;
      s = strptime(date, *format++, &tm);

      // parse nanoseconds
      if (s && *s=='.' && isdigit(s[1])) {
        s++;
        if (toys.optflags&FLAG_t)
          if (1 == sscanf(s, "%2u%n", &(tm.tm_sec), &len)) s += len;
        if (1 == sscanf(s, "%lu%n", &ts->tv_nsec, &len)) {
          s += len;
          if (ts->tv_nsec > 999999999) s = 0;
          else while (len++ < 9) ts->tv_nsec *= 10;
        }
      }
      if (s && !*s) break;
    }

    errno = 0;
    ts->tv_sec = mktime(&tm);
    if (!s || *s || ts->tv_sec == -1) perror_exit("bad '%s'", date);
  }
  ts[1]=ts[0];

  // Set time from -r?

  if (TT.file) {
    struct stat st;

    xstat(TT.file, &st);
    ts[0] = st.st_atim;
    ts[1] = st.st_mtim;
  }

  // Which time(s) should we actually change?
  i = toys.optflags & (FLAG_a|FLAG_m);
  if (i && i!=(FLAG_a|FLAG_m)) ts[i!=FLAG_m].tv_nsec = UTIME_OMIT;

  // Loop through files on command line
  for (ss = toys.optargs; *ss;) {
    char *s = *ss++;

    if (!strcmp(s, "-")) {
      if (!futimens(1, ts)) continue;
    } else {
      // cheat: FLAG_h is rightmost flag, so its value is 1
      if (!utimensat(AT_FDCWD, s, ts,
          (toys.optflags & FLAG_h)*AT_SYMLINK_NOFOLLOW)) continue;
      if (toys.optflags & FLAG_c) continue;
      if (access(s, F_OK) && (-1!=(fd = open(s, O_CREAT, 0666)))) {
        close(fd);
        if (toys.optflags) ss--;
        continue;
      }
    }
    perror_msg("'%s'", s);
  }
}
