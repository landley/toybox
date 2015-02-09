/* hwclock.c - get and set the hwclock
 *
 * Copyright 2014 Bilal Qureshi <bilal.jmi@gmail.com>
 *
 * No standard, but see Documentation/rtc.txt in the linux kernel source..
 *
USE_HWCLOCK(NEWTOY(hwclock, ">0(fast)f(rtc):u(utc)l(localtime)t(systz)w(systohc)s(hctosys)r(show)[-ul][!rtsw]", TOYFLAG_USR|TOYFLAG_BIN))

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

// Your system should have a /dev/rtc symlink. If your system is misconfigured,
// search /sys/class/rtc for RTC system clock set from, then try /dev/misc/rtc.
static int rtc_open(int flag)
{
  if (!TT.fname) {
    int fd; 

    if ((fd = open((TT.fname = "/dev/rtc"), flag)) != -1) return fd;
    TT.fname = 0;
    dirtree_read("/sys/class/rtc", rtc_find);
    if (TT.fname) return xopen(TT.fname, flag);
    TT.fname = "/dev/misc/rtc";
  }

  return xopen(TT.fname, flag);
}

// Get current time in seconds from rtc device. We could get subsecond time by
// waiting for next time change, but haven't implemented that yet.
static time_t get_rtc_seconds()
{
  struct tm time;
  time_t tm;
  char *ptz_old = ptz_old;
  int fd = rtc_open(O_RDONLY);

  xioctl(fd, RTC_RD_TIME, &time);
  close(fd);

  if (TT.utc) ptz_old = xtzset("UTC0");
  if ((tm = mktime(&time)) < 0) error_exit("mktime failed");
  if (TT.utc) {
    free(xtzset(ptz_old));
    free(ptz_old);
  }

  return tm;
}

void hwclock_main()
{
  struct timezone tzone;
  struct timeval timeval;

  // check for Grenich Mean Time
  if (toys.optflags & FLAG_u) TT.utc = 1;
  else {
    FILE *fp = fopen("/etc/adjtime", "r");

    if (fp) {
      for (;;) {
        char *line = 0;

        if (getline(&line, (void *)toybuf, fp) <= 0) break;
        TT.utc += !strncmp(line, "UTC", 3);
        free(line);
      }
      fclose(fp);
    }
  }

  if (toys.optflags & FLAG_w) {
    struct tm tm;
    int fd = rtc_open(O_WRONLY);

    if (gettimeofday(&timeval, 0)) perror_exit("gettimeofday");
    if ((TT.utc ? gmtime_r : localtime_r)(&timeval.tv_sec, &tm))
      error_exit("timeval");

    /* The value of tm_isdst will positive if daylight saving time is in effect,
     * zero if it is not and negative if the information is not available. 
     * */
    tm.tm_isdst = 0;
    xioctl(fd, RTC_SET_TIME, &time);
    close(fd);
  } else if (toys.optflags & FLAG_s) {
    tzone.tz_minuteswest = timezone / 60 - 60 * daylight;
    tzone.tz_dsttime = 0;
    timeval.tv_sec = get_rtc_seconds();
    timeval.tv_usec = 0;
    if (settimeofday(&timeval, &tzone) < 0) perror_exit("settimeofday");
  } else if (toys.optflags & FLAG_t) {
    struct tm *pb;

    if (gettimeofday(&timeval, NULL) < 0) perror_exit("gettimeofday");
    if (!(pb = localtime(&timeval.tv_sec))) error_exit("localtime failed");
    // extern long timezone is defined in header sys/time.h
    tzone.tz_minuteswest = timezone / 60;
    if (pb->tm_isdst) tzone.tz_minuteswest -= 60;
    tzone.tz_dsttime = 0; // daylight saving time is not in effect
    if (gettimeofday(&timeval, NULL) < 0) perror_exit("gettimeofday");
    if (!TT.utc) timeval.tv_sec += tzone.tz_minuteswest * 60;
    if (settimeofday(&timeval, &tzone) < 0) perror_exit("settimeofday");
  } else {
    time_t tm = get_rtc_seconds();
    char *pctm = ctime(&tm), *s = strrchr(pctm, '\n');

    if (s) *s = '\0';
    // TODO: implement this.
    xprintf("%s  0.000000 seconds\n", pctm);
  }
}
