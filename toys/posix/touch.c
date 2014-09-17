/* touch.c : change timestamp of a file
 *
 * Copyright 2012 Choubey Ji <warior.linux@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/touch.html 

USE_TOUCH(NEWTOY(touch, "acd:mr:t:[!dtr]", TOYFLAG_BIN))

config TOUCH
  bool "touch"
  default y
  help
    usage: touch [-amc] [-d DATE] [-t TIME] [-r FILE] FILE...

    Update the access and modification times of each FILE to the current time.

    -a	change access time
    -m	change modification time
    -c	don't create file
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

// Fetch access and/or modification time of a file
int fetch(char *file, struct timeval *tv, unsigned flags)
{
  struct stat st;

  if (stat(TT.file, &st)) return 1;

  if (flags & FLAG_a) {
    tv[0].tv_sec = st.st_atime;
    tv[0].tv_usec = st.st_atim.tv_nsec/1000;
  }
  if (flags & FLAG_m) {
    tv[1].tv_sec = st.st_mtime;
    tv[1].tv_usec = st.st_mtim.tv_nsec/1000;
  }

  return 0;
}

void touch_main(void)
{
  struct timeval tv[2];
  char **ss;
  int flag, fd, i;

  // Set time from clock?

  gettimeofday(tv, NULL);

  if (toys.optflags & (FLAG_t|FLAG_d)) {
    char *s, *date;
    struct tm tm;
    int len;

    localtime_r(&(tv->tv_sec), &tm);

    // Set time from -d?

    if (toys.optflags & FLAG_d) {
      date = TT.date;
      i = strlen(date);
      if (i) {
        s = strptime(date, "%Y-%m-%dT%T", &tm);
        if (s && *s=='.') {
          sscanf(s, ".%d%n", &i, &len);
          s += len;
          tv->tv_usec = i;
        }
      } else s = 0;

    // Set time from -t?

    } else {
      strcpy(toybuf, "%Y%m%d%H%M");
      date = TT.time;
      for (i=0;i<3;i++) {
        s = strptime(date, toybuf+(i&2), &tm);
        if (s) break;
        toybuf[1]='y';
      }
      if (s && *s=='.') {
        int count = sscanf(s, ".%2d%u%n", &(tm.tm_sec), &i, &len);

        if (count==2) tv->tv_usec = i;
        s += len;
      }
    }

    errno = 0;
    tv->tv_sec = mktime(&tm);
    if (!s || *s || errno == EOVERFLOW) perror_exit("bad '%s'", date);
  }
  tv[1]=tv[0];

  // Set time from -r?

  if (TT.file && fetch(TT.file, tv, FLAG_a|FLAG_m))
    perror_exit("-r '%s'", TT.file);

  // Ok, we've got a time. Flip -am flags so now it's the ones we _keep_.

  flag = (~toys.optflags) & (FLAG_m|FLAG_a);

  // Loop through files on command line
  for (ss=toys.optargs; *ss;) {
    if ((flag == (FLAG_m|FLAG_a) || !fetch(*ss, tv, flag)) && !utimes(*ss, tv))
      ss++;
    else if (toys.optflags & FLAG_c) ss++;
    else if (-1 != (fd = open(*ss, O_CREAT, 0666))) close(fd);
    else perror_msg("'%s'", *ss++);
  }
}
