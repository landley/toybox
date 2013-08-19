/* logger.c - Log messages.
 *
 * Copyright 2013 Ilya Kuzmich <ilya.kuzmich@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/logger.html

USE_LOGGER(NEWTOY(logger, "st:p:", TOYFLAG_USR|TOYFLAG_BIN))

config LOGGER
  bool "logger"
  depends on SYSLOGD
  default n
  help
    usage: logger [-s] [-t tag] [-p [facility.]priority] [message]

    Log message (or stdin) to syslog.
*/

#define FOR_logger
#include "toys.h"

GLOBALS(
  char *priority_arg;
  char *ident;
)

extern int logger_lookup(int where, char *key);

void logger_main(void)
{
  int facility = LOG_USER, priority = LOG_NOTICE;
  char *message = NULL;

  if (toys.optflags & FLAG_p) {
    char *sep = strchr(TT.priority_arg, '.');

    if (sep) {
      *sep = '\0';
      if ((facility = logger_lookup(0, TT.priority_arg)) == -1)
        error_exit("bad facility: %s", TT.priority_arg);
      TT.priority_arg = sep+1;
    }

    if ((priority = logger_lookup(1, TT.priority_arg)) == -1)
      error_exit("bad priority: %s", TT.priority_arg);
  }

  if (!(toys.optflags & FLAG_t)) {
    struct passwd *pw = getpwuid(geteuid());

    if (!pw) perror_exit("getpwuid");
    TT.ident = xstrdup(pw->pw_name);
  }

  if (toys.optc) {
    int length = 0, pos = 0;

    for (;*toys.optargs; toys.optargs++) {
      length += strlen(*(toys.optargs)) + 1; // plus one for the args spacing
      message = xrealloc(message, length + 1); // another one for the null byte

      sprintf(message + pos, "%s ", *toys.optargs);
      pos = length;
    }
  } else {
    toybuf[readall(0, toybuf, 4096-1)] = '\0';
    message = toybuf;
  }

  openlog(TT.ident, (toys.optflags & FLAG_s ? LOG_PERROR : 0) , facility);
  syslog(priority, "%s", message);
  closelog();
}
