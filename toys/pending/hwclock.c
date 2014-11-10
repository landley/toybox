/* hwclock.c - get and set the hwclock
 *
 * Copyright 2014 Bilal Qureshi <bilal.jmi@gmail.com>
 *
 * No Standard.
 *
USE_HWCLOCK(NEWTOY(hwclock, ">0(fast)f(rtc):u(utc)l(localtime)t(systz)w(systohc)s(hctosys)r(show)[!ul][!rsw]", TOYFLAG_USR|TOYFLAG_BIN))

config HWCLOCK
  bool "hwclock"
  default n
  help
    usage: hwclock [-rswtluf]

    -f FILE Use specified device file instead of /dev/rtc (--show)
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

static int rtc_open(int flag)
{
  if (!TT.fname) {
    int fd; 

    if ((fd = open((TT.fname = "/dev/rtc"), flag)) != -1) return fd;
    else if ((fd = open((TT.fname = "/dev/rtc0"), flag)) != -1) return fd;
    else TT.fname = "/dev/misc/rtc";
  }
  return xopen(TT.fname, flag);
}

static time_t get_rtc()
{
  struct tm time;
  time_t tm;
  char *ptz_old = 0;
  int fd = rtc_open(O_RDONLY);

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
  int fd = rtc_open(O_WRONLY);

  if (gettimeofday(&tmval, NULL) < 0) perror_exit("gettimeofday");
  // converting a time value to broken-down UTC time
  if (TT.utc && !gmtime_r((time_t*)&tmval.tv_sec, &time)) 
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

void hwclock_main()
{
  // check for UTC
  if (!(toys.optflags & FLAG_u)) {
    FILE *fp = fopen("/etc/adjtime", "r");

    if (fp) {
      char *line = NULL;
      size_t st;

      while (0 < getline(&line, &st, fp)) {
        if (!strncmp(line, "UTC", 3)) {
          TT.utc = 1;
          break;
        }
        free(line);
      }
      fclose(fp);
    }
  } else TT.utc = 1;

  if (toys.optflags & FLAG_w) set_hwclock_from_sysclock();
  else if (toys.optflags & FLAG_s) set_sysclock_from_hwclock(); 
  else if (toys.optflags & FLAG_t) set_sysclock_timezone();
  else if ((toys.optflags & FLAG_r) || (toys.optflags & FLAG_l) 
      || !*toys.optargs)
  {
    time_t tm = get_rtc();
    char *s, *pctm = ctime(&tm);

    // ctime() is defined as equivalent to asctime(localtime(t)),
    // which is defined to overflow its buffer rather than return NULL.
    // if (!pctm) error_exit("can't happen");
    if ((s = strrchr(pctm, '\n'))) *s = '\0';
    // TODO: implement this.
    xprintf("%s  0.000000 seconds\n", pctm);
  }
}
