/* cal.c - show calendar.
 *
 * Copyright 2011 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/cal.html

USE_CAL(NEWTOY(cal, ">2", TOYFLAG_USR|TOYFLAG_BIN))

config CAL
  bool "cal"
  default y
  help
    usage: cal [[month] year]

    Print a calendar.

    With one argument, prints all months of the specified year.
    With two arguments, prints calendar for month and year.
*/

#include "toys.h"

// Write calendar into buffer: each line is 20 chars wide, end indicated
// by empty string.

static char *calstrings(char *buf, struct tm *tm)
{
  char temp[21];
  int wday, mday, start, len, line;

  // header
  len = strftime(temp, 21, "%B %Y", tm);
  len += (20-len)/2;
  buf += sprintf(buf, "%*s%*s ", len, temp, 20-len, "");
  buf++;
  buf += sprintf(buf, "Su Mo Tu We Th Fr Sa ");
  buf++;

  // What day of the week does this month start on?
  if (tm->tm_mday>1)
    start = (36+tm->tm_wday-tm->tm_mday)%7;
  else start = tm->tm_wday;

  // What day does this month end on?  Alas, libc doesn't tell us...
  len = 31;
  if (tm->tm_mon == 1) {
    int year = tm->tm_year;
    len = 28;
    if (!(year & 3) && !((year&100) && !(year&400))) len++;
  } else if ((tm->tm_mon+(tm->tm_mon>6 ? 1 : 0)) & 1) len = 30;

  for (mday=line=0;line<6;line++) {
    for (wday=0; wday<7; wday++) {
      char *pat = "   ";
      if (!mday ? wday==start : mday<len) {
        pat = "%2d ";
        mday++;
      }
      buf += sprintf(buf, pat, mday);
    }
    buf++;
  }

  return buf;
}

void xcheckrange(long val, long low, long high)
{
  char *err = "%ld %s than %ld";

  if (val < low) error_exit(err, val, "less", low);
  if (val > high) error_exit(err, val, "greater", high);
}

// Worst case scenario toybuf usage: sizeof(struct tm) plus 21 bytes/line
// plus 8 lines/month plus 12 months, comes to a bit over 2k of our 4k buffer.

void cal_main(void)
{
  struct tm *tm;
  char *buf = toybuf;

  if (toys.optc) {
    // Conveniently starts zeroed
    tm = (struct tm *)toybuf;
    buf += sizeof(struct tm);

    // Last argument is year, one before that (if any) is month.
    xcheckrange(tm->tm_year = atol(toys.optargs[--toys.optc]),1,9999);
    tm->tm_year -= 1900;
    tm->tm_mday = 1;
    tm->tm_hour = 12;  // noon to avoid timezone weirdness
    if (toys.optc) {
      xcheckrange(tm->tm_mon = atol(toys.optargs[--toys.optc]),1,12);
      tm->tm_mon--;

    // Print 12 months of the year

    } else {
      char *bufs[12];
      int i, j, k;

      for (i=0; i<12; i++) {
        tm->tm_mon=i;
        mktime(tm);
        buf = calstrings(bufs[i]=buf, tm);
      }

      // 4 rows, 6 lines each, 3 columns
      for (i=0; i<4; i++) {
        for (j=0; j<8; j++) {
          for(k=0; k<3; k++) {
            char **b = bufs+(k+i*3);
            *b += printf("%s ", *b);
          }
          puts("");
        }
      }
      return;
    }

    // What day of the week does that start on?
    mktime(tm);

  } else {
    time_t now;
    time(&now);
    tm = localtime(&now);
  }

  calstrings(buf, tm);
  while (*buf) buf += printf("%s\n", buf);
}
