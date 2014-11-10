/* hwclock.c - get and set the hwclock
 *
 * Copyright 2014 Bilal Qureshi <bilal.jmi@gmail.com>
 *
 * No Standard.
 *
USE_HWCLOCK(NEWTOY(hwclock, "f(rtc):u(utc)l(localtime)t(systz)w(systohc)s(hctosys)r(show)[!ul][!rs][!rw][!rt][!sw][!st][!wt]", TOYFLAG_USR|TOYFLAG_BIN))

config HWCLOCK
  bool "hwclock"
  default n
  help
    usage: hwclock [-r|--show] [-s|--hctosys] [-w|--systohc] [-t|--systz] [-l|--localtime] 
    [-u|--utc] [-f|--rtc FILE]

    -f FILE Use specified device file (e.g. /dev/rtc2) instead of default
    -l      Assume hardware clock is kept in localtime
    -r      Show hardware clock time
    -s      Set system time from hardware clock
    -t      Set the system time based on the current timezone 
    -u      Assume hardware clock is kept in UTC
    -w      Set hardware clock from system time
*/
#define FOR_hwclock
#include "toys.h"
#include <linux/rtc.h>

GLOBALS(
  char *fname;

  int utc;
)

static int rtc_open(char **dev_rtc, int flag)
{
  if (!*dev_rtc) {
    int fd; 

    if ((fd = open((*dev_rtc = "/dev/rtc"), flag)) != -1) return fd;
    else if ((fd = open((*dev_rtc = "/dev/rtc0"), flag)) != -1) return fd;
    else *dev_rtc = "/dev/misc/rtc";
  }
  return xopen(*dev_rtc, flag);
}

static time_t get_rtc()
{
  struct tm time;
  time_t tm;
  char *ptz_old = NULL;
  int fd = rtc_open(&TT.fname, O_RDONLY);

  memset(&time, 0, sizeof(time));
  xioctl(fd, RTC_RD_TIME, &time);
  close(fd);
  if (TT.utc) {
    ptz_old = getenv("TZ");
    if (putenv((char*)"TZ=UTC0")) perror_exit("putenv");
    tzset();
  }
  if ((tm = mktime(&time)) < 0) error_exit("mktime failed");
  if (TT.utc) {
    if (unsetenv("TZ") < 0) perror_exit("unsetenv");
    if (ptz_old && putenv(ptz_old - 3)) perror_exit("putenv");
    tzset();
  }
  return tm;
}

static void set_sysclock_from_hwclock()
{
  struct timezone tmzone;
  struct timeval tmval;

  tmzone.tz_minuteswest = timezone / 60 - 60 * daylight;
  tmzone.tz_dsttime = 0;
  tmval.tv_sec = get_rtc();
  tmval.tv_usec = 0;
  if (settimeofday(&tmval, &tmzone) < 0) perror_exit("settimeofday");
}

static void set_hwclock_from_sysclock()
{
  struct timeval tmval;
  struct tm time;
  int fd = rtc_open(&TT.fname, O_WRONLY);

  if (gettimeofday(&tmval, NULL) < 0) perror_exit("gettimeofday");
  // converting a time value to broken-down UTC time
  if (TT. utc && !gmtime_r((time_t*)&tmval.tv_sec, &time)) 
    error_exit("gmtime_r failed");
  // converting a time value to a broken-down localtime
  else if (!(localtime_r((time_t*)&tmval.tv_sec, &time)))
    error_exit("localtime_r failed");

  /* The value of tm_isdst will positive if daylight saving time is in effect,
   * zero if it is not and negative if the information is not available. 
   * */
  time.tm_isdst = 0;
  xioctl(fd, RTC_SET_TIME, &time);
  close(fd);
}

static void set_sysclock_timezone()
{
  struct timezone tmzone;
  struct timeval tmval;
  struct tm *pb;

  if (gettimeofday(&tmval, NULL) < 0) perror_exit("gettimeofday");
  if (!(pb = localtime(&tmval.tv_sec))) error_exit("localtime failed");
  // extern long timezone => defined in header sys/time.h
  tmzone.tz_minuteswest = timezone / 60;
  if (pb->tm_isdst) tmzone.tz_minuteswest -= 60;
  tmzone.tz_dsttime = 0; // daylight saving time is not in effect
  if (gettimeofday(&tmval, NULL) < 0) perror_exit("gettimeofday");
  if (!TT.utc) tmval.tv_sec += tmzone.tz_minuteswest * 60;
  if (settimeofday(&tmval, &tmzone) < 0) perror_exit("settimeofday");
}

static void rtc_adjtime()
{
  char *line = NULL;
  int fd = open("/etc/adjtime", O_RDONLY);

  if (fd != -1) {
    for (; (line = get_line(fd)); free(line)) {
      if (!strncmp(line, "UTC", 3)) {
        TT.utc = 1;
        break;
      }
    }
    close(fd);
  }
}

static void display_hwclock()
{
  time_t tm = get_rtc();
  char *s, *pctm = ctime(&tm);

  if (pctm) {
    if ((s = strrchr(pctm, '\n'))) *s = '\0';
    xprintf("%s  0.000000 seconds\n", pctm);
  } else error_exit("failed to convert a time value to a date & time string");
}

void hwclock_main()
{
  (!(toys.optflags & FLAG_u)) ? rtc_adjtime() : (TT.utc = 1); // check for UTC
  if (toys.optflags & FLAG_w) set_hwclock_from_sysclock();
  else if (toys.optflags & FLAG_s) set_sysclock_from_hwclock(); 
  else if (toys.optflags & FLAG_t) set_sysclock_timezone();
  else if ((toys.optflags & FLAG_r) || (toys.optflags & FLAG_l) 
      || !*toys.optargs) display_hwclock();
  else {
    toys.exithelp++;
    error_exit("invalid option '%s'", *toys.optargs);
  }
}
