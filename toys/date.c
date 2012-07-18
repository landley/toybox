/* vi: set sw=4 ts=4:
 *
 * date.c - set/get the date
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/date.html

USE_DATE(NEWTOY(date, "r:u", TOYFLAG_BIN))

config DATE
	bool "date"
	default y
	help
          usage: date [-u] [+format] | mmddhhmm[[cc]yy]

	  Set/get the current date/time
*/

#include "toys.h"

/* Convert a string of decimal numbers to their integer equivalent */
static int fromdec(const char *buf, int len)
{
    int result = 0;
    while (len--) result=result * 10 + (*buf++ - '0');
    return result;
}

void date_main(void)
{
    const char *format_string = "%a %b %e %H:%M:%S %Z %Y";

    /* Check if we should be displaying the date */
    if (!toys.optargs[0] || toys.optargs[0][0] == '+') {
        time_t now = time(NULL);
        struct tm *tm;

        if (toys.optargs[0]) format_string = toys.optargs[0]+1;
        if (toys.optflags) tm = gmtime(&now);
        else tm = localtime(&now);
        if (!tm) perror_msg("Unable to retrieve current time");
        if (!strftime(toybuf, sizeof(toybuf), format_string, tm))
            perror_msg("bad format `%s'", format_string);
        puts(toybuf);
    } else {
        int len = strlen(toys.optargs[0]);
        struct tm tm;
        struct timeval tv;

        if (len < 8 || len > 12 || len & 1)
            error_msg("bad date `%s'", toys.optargs[0]);
        memset(&tm, 0, sizeof(tm));
        /* Date format: mmddhhmm[[cc]yy] */
        tm.tm_mon = fromdec(toys.optargs[0], 2) - 1;
        tm.tm_mday = fromdec(&toys.optargs[0][2], 2);
        tm.tm_hour = fromdec(&toys.optargs[0][4], 2);
        tm.tm_min = fromdec(&toys.optargs[0][6], 2);
        if (len == 12) tm.tm_year = fromdec(&toys.optargs[0][8], 4) - 1900;
        else if (len == 10) {
            tm.tm_year = fromdec(&toys.optargs[0][8], 2);
            /* 69-99 = 1969-1999, 0 - 68 = 2000-2068 */
            if (tm.tm_year < 69) tm.tm_year += 100;
        } else {
            /* Year not specified, so retrieve current year */
            time_t now = time(NULL);
            struct tm *now_tm = localtime(&now);
            if (!now_tm) perror_msg("Unable to retrieve current time");
            tm.tm_year = now_tm->tm_year;
        }
        if (!toys.optflags) tv.tv_sec = mktime(&tm);
        else {
            /* Get the UTC version of a struct tm */
            char *tz = NULL;
            tz = getenv("TZ");
            setenv("TZ", "", 1);
            tzset();
            tv.tv_sec = mktime(&tm);
            if (tz) setenv("TZ", tz, 1);
            else unsetenv("TZ");
            tzset();
        }

        if (tv.tv_sec == (time_t)-1)
            error_msg("bad `%s'", toys.optargs[0]);
        tv.tv_usec = 0;
        if (!strftime(toybuf, sizeof(toybuf), format_string, &tm))
            perror_msg("bad format `%s'", format_string);
        puts(toybuf);
        if (settimeofday(&tv, NULL) < 0) perror_msg("cannot set date");
    }
}
