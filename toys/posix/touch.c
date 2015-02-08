/* touch.c : change timestamp of a file
 *
 * Copyright 2012 Choubey Ji <warior.linux@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/touch.html 

USE_TOUCH(NEWTOY(touch, "acd:mr:t:h[!dtr]", TOYFLAG_BIN))

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
    char *s, *date;
    struct tm tm;
    int len = 0;

    localtime_r(&(ts->tv_sec), &tm);

    // Set time from -d?

    if (toys.optflags & FLAG_d) {
      date = TT.date;
      i = strlen(date);
      if (i) {
        // Trailing Z means UTC timezone, don't expect libc to know this.
        if (toupper(date[i-1])=='Z') {
          date[i-1] = 0;
          setenv("TZ", "UTC0", 1);
          localtime_r(&(ts->tv_sec), &tm);
        }
        s = strptime(date, "%Y-%m-%dT%T", &tm);
        if (s && *s=='.' && isdigit(s[1])) 
          sscanf(s, ".%lu%n", &ts->tv_nsec, &len);
        else len = 0;
      } else s = 0;

    // Set time from -t?

    } else {
      strcpy(toybuf, "%Y%m%d%H%M");
      date = TT.time;
      i = ((s = strchr(date, '.'))) ? s-date : strlen(date);
      if (i < 8 || i%2) error_exit("bad '%s'", date);
      for (i=0;i<3;i++) {
        s = strptime(date, toybuf+(i&2), &tm);
        if (s) break;
        toybuf[1]='y';
      }
      if (s && *s=='.' && sscanf(s, ".%2u%n", &(tm.tm_sec), &len) == 1) {
        sscanf(s += len, "%lu%n", &ts->tv_nsec, &len);
        len++;
      } else len = 0;
    }
    if (len) {
      s += len;
      if (ts->tv_nsec > 999999999) s = 0;
      else while (len++ < 10) ts->tv_nsec *= 10;
    }

    errno = 0;
    ts->tv_sec = mktime(&tm);
    if (!s || *s || errno == EOVERFLOW) perror_exit("bad '%s'", date);
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
  if (i && i!=(FLAG_a|FLAG_m)) ts[i==FLAG_m].tv_nsec = UTIME_OMIT;

  // Loop through files on command line
  for (ss = toys.optargs; *ss;) {

    // cheat: FLAG_h is rightmost flag, so its value is 1
    if (!utimensat(AT_FDCWD, *ss, ts,
        (toys.optflags & FLAG_h)*AT_SYMLINK_NOFOLLOW)) ss++;
    else if (toys.optflags & FLAG_c) ss++;
    else if (access(*ss, F_OK) && (-1!=(fd = open(*ss, O_CREAT, 0666))))
      close(fd);
    else perror_msg("'%s'", *ss++);
  }
}
