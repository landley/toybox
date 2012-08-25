/* vi: set sw=4 ts=4:
 *
 * uptime.c - Tell how long the system has been running.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_UPTIME(NEWTOY(uptime, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config UPTIME
	bool "uptime"
	default y
	help
	  usage: uptime

	  Tell how long the system has been running and the system load
	  averages for the past 1, 5 and 15 minutes.
*/

#include "toys.h"

void uptime_main(void)
{
	struct sysinfo info;
	time_t tmptime;
	struct tm * now;
	unsigned int days, hours, minutes;

	// Obtain the data we need.
	sysinfo(&info);
	time(&tmptime);
	now = localtime(&tmptime);

	// Time
	xprintf(" %02d:%02d:%02d up ", now->tm_hour, now->tm_min, now->tm_sec);
	// Uptime
	info.uptime /= 60;
	minutes = info.uptime%60;
	info.uptime /= 60;
	hours = info.uptime%24;
	days = info.uptime/24;
	if (days) xprintf("%d day%s, ", days, (days!=1)?"s":"");
	if (hours)
		xprintf("%2d:%02d, ", hours, minutes);
	else
		printf("%d min, ", minutes);

	printf(" load average: %.02f %.02f %.02f\n", info.loads[0]/65536.0,
		info.loads[1]/65536.0, info.loads[2]/65536.0);

}
