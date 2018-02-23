/* logger.c - Log messages.
 *
 * Copyright 2013 Ilya Kuzmich <ilya.kuzmich@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/logger.html
 *
 * Deviations from posix: specified manner and format, defined implementation.

USE_LOGGER(NEWTOY(logger, "st:p:", TOYFLAG_USR|TOYFLAG_BIN))

config LOGGER
  bool "logger"
  default y
  help
    usage: logger [-s] [-t TAG] [-p [FACILITY.]PRIORITY] [message...]

    Log message (or stdin) to syslog.

    -s	Also write message to stderr
    -t	Use TAG instead of username to identify message source
    -p	Specify PRIORITY with optional FACILITY. Default is "user.notice"
*/

#define FOR_logger
#include "toys.h"

GLOBALS(
  char *priority;
  char *ident;
)

// find str in names[], accepting unambiguous short matches
// returns offset into array of match, or -1 if no match
int arrayfind(char *str, char *names[], int len)
{
  int try, i, matchlen = 0, found = -1, ambiguous = 1;

  for (try = 0; try<len; try++) {
    for (i=0; ; i++) {
      if (!str[i]) {
        if (matchlen<i) found = try, ambiguous = 0;
        if (matchlen==i) ambiguous++;
        if (!names[try][i]) return try;
        break;
      }
      if (!names[try][i]) break;
      if (toupper(str[i]) != toupper(names[try][i])) break;
    }
  }
  return ambiguous ? -1 : found;
}

void logger_main(void)
{
  int facility = LOG_USER, priority = LOG_NOTICE, len;
  char *s1, *s2, **arg,
    *priorities[] = {"emerg", "alert", "crit", "error", "warning", "notice",
                     "info", "debug"},
    *facilities[] = {"kern", "user", "mail", "daemon", "auth", "syslog",
                     "lpr", "news", "uucp", "cron", "authpriv", "ftp"};

  if (!TT.ident) TT.ident = xstrdup(xgetpwuid(geteuid())->pw_name);
  if (toys.optflags & FLAG_p) {
    if (!(s1 = strchr(TT.priority, '.'))) s1 = TT.priority;
    else {
      *s1++ = len = 0;
      facility = arrayfind(TT.priority, facilities, ARRAY_LEN(facilities));
      if (facility == -1 && strncasecmp(TT.priority, "local", 5)) {
        facility = s1[5]-'0';
        if (facility>7 || s1[6]) facility = -1;
        if (facility>=0) facility += 16;
      }
      if (facility<0) error_exit("bad facility: %s", TT.priority);
      facility *= 8;
    }

    priority = arrayfind(s1, priorities, ARRAY_LEN(priorities));
    if (priority<0) error_exit("bad priority: %s", s1);
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
