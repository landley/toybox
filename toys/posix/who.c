/* who.c - display who is on the system
 *
 * Copyright 2012 ProFUSION Embedded Systems
 *
 * by Luis Felipe Strano Moraes <lfelipe@profusion.mobi>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/who.html
 *
 * Posix says to support many options (-abdHlmpqrstTu) but this
 * isn't aimed at minicomputers with modem pools.

USE_WHO(NEWTOY(who, "a", TOYFLAG_USR|TOYFLAG_BIN))

config WHO
  bool "who"
  default y
  depends on TOYBOX_UTMPX
  help
    usage: who

    Print logged user information on system
*/

#define FOR_who
#include "toys.h"

void who_main(void)
{
  struct utmpx *entry;

  setutxent();

  while ((entry = getutxent())) {
    if ((toys.optflags & FLAG_a) || entry->ut_type == USER_PROCESS) {
      time_t time;
      int time_size;
      char *times;

      time = entry->ut_tv.tv_sec;
      times = ctime(&time);
      time_size = strlen(times) - 2;
      printf("%s\t%s\t%*.*s\t(%s)\n", entry->ut_user, entry->ut_line,
        time_size, time_size, ctime(&time), entry->ut_host);
    }
  }

  endutxent();
}
