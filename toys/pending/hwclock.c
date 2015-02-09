/* hwclock.c - get and set the hwclock
 *
 * Copyright 2014 Bilal Qureshi <bilal.jmi@gmail.com>
 *
 * No standard, but see Documentation/rtc.txt in the linux kernel source..
 *
USE_HWCLOCK(NEWTOY(hwclock, ">0(fast)f(rtc):u(utc)l(localtime)t(systz)s(hctosys)r(show)w(systohc)[-ul][!rtsw]", TOYFLAG_USR|TOYFLAG_BIN))

config HWCLOCK
  bool "hwclock"
  default n
  help
    usage: hwclock [-rswtluf]

    -f FILE Use specified device file instead of /dev/rtc (--rtc)
    -l      Hardware clock uses localtime (--localtime)
    -r      Show hardware clock time (--show)
    -s      Set system time from hardware clock (--hctosys)
    -t      Set the system time based on the current timezone (--systz)
    -u      Hardware clock uses UTC (--utc)
    -w      Set hardware clock from system time (--systohc)
*/

#define FOR_hwclock
#include "toys.h"
#include <linux/rtc.h>

GLOBALS(
  char *fname;

  int utc;
)

static int rtc_find(struct dirtree* node)
{
  FILE *fp;

  if (!node->parent) return DIRTREE_RECURSE;

  snprintf(toybuf, sizeof(toybuf), "/sys/class/rtc/%s/hctosys", node->name);
  fp = fopen(toybuf, "r");
  if (fp) {
    int hctosys = 0, items = fscanf(fp, "%d", &hctosys);

    fclose(fp);
    if (items == 1 && hctosys == 1) {
      snprintf(toybuf, sizeof(toybuf), "/dev/%s", node->name);
      TT.fname = toybuf;

      return DIRTREE_ABORT;
    }
  }

  return 0;
}

void hwclock_main()
{
  struct timezone tzone;
  struct timeval timeval;
  struct tm tm;
  time_t time;
  int fd = -1;

  // check for Grenich Mean Time
  if (toys.optflags & FLAG_u) TT.utc = 1;
  else {
    FILE *fp;
    char *s = 0;

    for (fp = fopen("/etc/adjtime", "r");
         fp && getline(&s, (void *)toybuf, fp)>0;
         free(s), s = 0) TT.utc += !strncmp(s, "UTC", 3);
    if (fp) fclose(fp);
  }

  if (!(toys.optflags&FLAG_t)) {
    int w = toys.optflags & FLAG_w, flag = O_WRONLY*w;

    // Open /dev/rtc (if your system has no /dev/rtc symlink, search for it).
    if (!TT.fname && (fd = open("/dev/rtc", flag)) == -1) {
      dirtree_read("/sys/class/rtc", rtc_find);
      if (!TT.fname) TT.fname = "/dev/misc/rtc";
    }
    if (fd == -1) fd = xopen(TT.fname, flag);

    // Get current time in seconds from rtc device. todo: get subsecond time
    if (!w) {
      char *s = s;

      xioctl(fd, RTC_RD_TIME, &tm);
      if (TT.utc) s = xtzset("UTC0");
      if ((time = mktime(&tm)) < 0) goto bad;
      if (TT.utc) {
        free(xtzset(s));
        free(s);
      }
    }
  }

  if (toys.optflags & (FLAG_w|FLAG_t))
    if (gettimeofday(&timeval, 0)
        || (TT.utc ? gmtime_r : localtime_r)(&timeval.tv_sec, &tm)) goto bad;

  if (toys.optflags & FLAG_w) {
    /* The value of tm_isdst will positive if daylight saving time is in effect,
     * zero if it is not and negative if the information is not available. 
     * todo: so why isn't this negative...? */
    tm.tm_isdst = 0;
    xioctl(fd, RTC_SET_TIME, &time);
  } else if (toys.optflags & FLAG_s) {
    tzone.tz_minuteswest = timezone / 60 - 60 * daylight;
    timeval.tv_sec = time;
    timeval.tv_usec = 0; // todo: fixit
  } else if (toys.optflags & FLAG_t) {
    // Adjust seconds for timezone and daylight saving time
    // extern long timezone is defined in header sys/time.h
    tzone.tz_minuteswest = timezone / 60;
    if (tm.tm_isdst) tzone.tz_minuteswest -= 60;
    if (!TT.utc) timeval.tv_sec += tzone.tz_minuteswest * 60;
  } else {
    char *c = ctime(&time), *s = strrchr(c, '\n');

    if (s) *s = '\0';
    // TODO: implement this.
    xprintf("%s  0.000000 seconds\n", c);
  }
  if (toys.optflags & (FLAG_t|FLAG_s)) {
    tzone.tz_dsttime = 0;
    if (settimeofday(&timeval, &tzone)) goto bad;
  }

  if (fd != -1) close(fd);

  return;
bad:
  perror_exit("failed");
}
