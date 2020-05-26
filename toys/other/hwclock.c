/* hwclock.c - get and set the hwclock
 *
 * Copyright 2014 Bilal Qureshi <bilal.jmi@gmail.com>
 *
 * No standard, but see Documentation/rtc.txt in the linux kernel source..
 *
USE_HWCLOCK(NEWTOY(hwclock, ">0(fast)f(rtc):u(utc)l(localtime)t(systz)s(hctosys)r(show)w(systohc)[-ul][!rtsw]", TOYFLAG_SBIN))

config HWCLOCK
  bool "hwclock"
  default y
  help
    usage: hwclock [-rswtluf]

    Get/set the hardware clock.

    -f FILE	Use specified device file instead of /dev/rtc (--rtc)
    -l	Hardware clock uses localtime (--localtime)
    -r	Show hardware clock time (--show)
    -s	Set system time from hardware clock (--hctosys)
    -t	Set the system time based on the current timezone (--systz)
    -u	Hardware clock uses UTC (--utc)
    -w	Set hardware clock from system time (--systohc)
*/

#define FOR_hwclock
#include "toys.h"
#include <linux/rtc.h>

GLOBALS(
  char *f;
)

static int rtc_find(struct dirtree* node)
{
  FILE *fp;

  if (!node->parent) return DIRTREE_RECURSE;

  sprintf(toybuf, "/sys/class/rtc/%s/hctosys", node->name);
  fp = fopen(toybuf, "r");
  if (fp) {
    int hctosys = 0, items = fscanf(fp, "%d", &hctosys);

    fclose(fp);
    if (items == 1 && hctosys == 1) {
      sprintf(toybuf, "/dev/%s", node->name);
      TT.f = toybuf;

      return DIRTREE_ABORT;
    }
  }

  return 0;
}

void hwclock_main()
{
  struct timespec ts;
  struct tm tm;
  int fd = -1, utc;

  if (FLAG(u)) utc = 1;
  else if (FLAG(l)) utc = 0;
  else {
    xreadfile("/etc/adjtime", toybuf, sizeof(toybuf));
    utc = !!strstr(toybuf, "UTC");
  }

  if (!FLAG(t)) {
    int flag = O_WRONLY*FLAG(w);

    // Open /dev/rtc (if your system has no /dev/rtc symlink, search for it).
    if (!TT.f && (fd = open("/dev/rtc", flag)) == -1) {
      dirtree_read("/sys/class/rtc", rtc_find);
      if (!TT.f) TT.f = "/dev/misc/rtc";
    }
    if (fd == -1) fd = xopen(TT.f, flag);

    // Get current time in seconds from rtc device. todo: get subsecond time
    if (!FLAG(w)) {
      xioctl(fd, RTC_RD_TIME, &tm);
      ts.tv_sec = xmktime(&tm, utc);
      ts.tv_nsec = 0; // todo: fixit
    }
  }

  if (FLAG(w) || FLAG(t)) {
    if (clock_gettime(CLOCK_REALTIME, &ts)) perror_exit("clock_gettime failed");
    if (!(utc ? gmtime_r : localtime_r)(&ts.tv_sec, &tm))
      error_exit(utc ? "gmtime_r failed" : "localtime_r failed");
  }

  if (FLAG(w)) {
    /* The value of tm_isdst is positive if daylight saving time is in effect,
     * zero if it is not and negative if the information is not available.
     * todo: so why isn't this negative...? */
    tm.tm_isdst = 0;
    xioctl(fd, RTC_SET_TIME, &tm);
  } else if (FLAG(t) || FLAG(s)) {
    if (clock_settime(CLOCK_REALTIME, &ts)) perror_exit("clock_settime failed");
  } else {
    strftime(toybuf, sizeof(toybuf), "%F %T%z", &tm);
    xputs(toybuf);
  }

  if (fd != -1) close(fd);
}
