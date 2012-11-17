/* vi: set sw=4 ts=4:
 *
 * touch.c : change timestamp of a file
 * Copyright 2012 Choubey Ji <warior.linux@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/touch.html 

USE_TOUCH(NEWTOY(touch, "mr:t:", TOYFLAG_BIN))

config TOUCH
  bool "th"
  default y
  help
    Usage: Usage: touch [OPTION]... FILE...
    Update the access and modification times of each FILE to the current time.
    -m                     change only the modification time
    -r, --reference=FILE   use this file's times instead of current time
    -t STAMP               use [[CC]YY]MMDDhhmm[.ss] instead of current time
*/

#define FOR_touch
#include "toys.h"

GLOBALS(
  char *date;
  char *file;
)

void touch_main(void)
{
  int fd;
  time_t now;
  struct utimbuf modinfo;
  struct stat st;

  if (TT.date) {
    struct tm *tm = getdate(TT.date);

    if (!tm) perror_exit("bad date '%s'", TT.date);
    now = mktime(tm);
  } else time(&now);
  modinfo.modtime = now;
  modinfo.actime = now;

  if (TT.file) {
    xstat(TT.file, &st);
    modinfo.modtime = st.st_mtime;
    modinfo.actime = st.st_atime;
  }

  if (toys.optflags & FLAG_m) {
    if(stat(toys.optargs[toys.optc - 1], &st) < 0) {
      toys.exitval = EXIT_FAILURE;
      return;
    }
    modinfo.actime = st.st_atime;
    if(!(toys.optflags & (FLAG_r|FLAG_t))) {
      time(&now);
      modinfo.modtime = now;
    }
  }
  if (utime(toys.optargs[toys.optc - 1], &modinfo) == -1) {
    if ((fd = open(toys.optargs[toys.optc - 1],O_CREAT |O_RDWR, 0644)) != -1) {
      close(fd);
      utime(toys.optargs[toys.optc - 1], &modinfo);
    } else {
      perror_msg("can't create '%s'", toys.optargs[toys.optc-1]);
      toys.exitval = EXIT_FAILURE;
    }
  }
}
