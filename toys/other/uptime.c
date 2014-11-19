/* uptime.c - Tell how long the system has been running.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 * Copyright 2012 Luis Felipe Strano Moraes <lfelipe@profusion.mobi>
 * Copyright 2013 Jeroen van Rijn <jvrnix@gmail.com>


USE_UPTIME(NEWTOY(uptime, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config UPTIME
  bool "uptime"
  default y
  depends on TOYBOX_UTMPX
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
  struct utmpx *entry;
  int users = 0;

  // Obtain the data we need.
  sysinfo(&info);
  time(&tmptime);
  now = localtime(&tmptime);

  // Obtain info about logged on users
  setutxent();
  while ((entry = getutxent())) if (entry->ut_type == USER_PROCESS) users++;
  endutxent();

  // Time
  xprintf(" %02d:%02d:%02d up ", now->tm_hour, now->tm_min, now->tm_sec);
  // Uptime
  info.uptime /= 60;
  minutes = info.uptime%60;
  info.uptime /= 60;
  hours = info.uptime%24;
  days = info.uptime/24;
  if (days) xprintf("%d day%s, ", days, (days!=1)?"s":"");
  if (hours) xprintf("%2d:%02d, ", hours, minutes);
  else printf("%d min, ", minutes);
  printf(" %d user%s, ", users, (users!=1) ? "s" : "");
  printf(" load average: %.02f, %.02f, %.02f\n", info.loads[0]/65536.0,
    info.loads[1]/65536.0, info.loads[2]/65536.0);
}
