/* logger.c - Log messages.
 *
 * Copyright 2013 Ilya Kuzmich <ilya.kuzmich@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/logger.html

USE_LOGGER(NEWTOY(logger, "st:p:", TOYFLAG_USR|TOYFLAG_BIN))

config LOGGER
  bool "logger"
  default n
  help
    usage: logger [-s] [-t tag] [-p [facility.]priority] [message]

    Log message (or stdin) to syslog.
*/

#define FOR_logger
#include "toys.h"

GLOBALS(
  char *priority;
  char *ident;
)

void logger_main(void)
{
  int facility = LOG_USER, priority = LOG_NOTICE, len;
  char *s1, *s2, **arg;
  CODE *code;

  if (!TT.ident) TT.ident = xstrdup(xgetpwuid(geteuid())->pw_name);
  if (toys.optflags & FLAG_p) {
    if (!(s1 = strchr(TT.priority, '.'))) s1 = TT.priority;
    else {
      *s1++ = 0;
      for (code = facilitynames; code->c_name; code++)
        if (!strcasecmp(TT.priority, code->c_name)) break;
      if (!code->c_name) error_exit("bad facility: %s", TT.priority);
      facility = code->c_val;
    }

    for (code = prioritynames; code->c_name; code++)
      if (!strcasecmp(s1, code->c_name)) break;
    if (!code->c_name) error_exit("bad priority: %s", s1);
  }


  if (toys.optc) {
    for (len = 0, arg = toys.optargs; *arg; arg++) len += strlen(*arg)+1;
    s1 = s2 = xmalloc(len);
    for (arg = toys.optargs; *arg; arg++) {
      if (arg != toys.optargs) *s2++ = ' ';
      s2 = stpcpy(s2, *arg);
    }
  } else {
    toybuf[readall(0, toybuf, sizeof(toybuf)-1)] = 0;
    s1 = toybuf;
  }

  openlog(TT.ident, LOG_PERROR*!!(toys.optflags&FLAG_s), facility);
  syslog(priority, "%s", s1);
  closelog();
}
