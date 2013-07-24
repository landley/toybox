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
    usage: hello [-s] [-t tag] [-p [facility.]priority] [message]

    Log message (or stdin) to syslog.
*/

#define FOR_logger
#include "toys.h"
#include <syslog.h>

GLOBALS(
  char *priority_arg;
  char *ident;
)

struct mapping {
  char *key;
  int value;
};

static struct mapping facilities[] = {
  {"user", LOG_USER}, {"main", LOG_MAIL}, {"news", LOG_NEWS},
  {"uucp", LOG_UUCP}, {"daemon", LOG_DAEMON}, {"auth", LOG_AUTH},
  {"cron", LOG_CRON}, {"lpr", LOG_LPR}, {"local0", LOG_LOCAL0},
  {"local1", LOG_LOCAL1}, {"local2", LOG_LOCAL2}, {"local3", LOG_LOCAL3},
  {"local4", LOG_LOCAL4}, {"local5", LOG_LOCAL5}, {"local6", LOG_LOCAL6},
  {"local7", LOG_LOCAL7},
  {NULL, 0}
};

static struct mapping priorities[] = {
  {"emerg", LOG_EMERG}, {"alert", LOG_ALERT}, {"crit", LOG_CRIT},
  {"err", LOG_ERR}, {"warning", LOG_WARNING}, {"notice", LOG_NOTICE},
  {"info", LOG_INFO}, {"debug", LOG_DEBUG},
  {NULL, 0}
};

static int lookup(struct mapping *where, char *key)
{
  for (; where->key; where++)
    if (!strcasecmp(key, where->key)) return where->value;

  return -1;
}

void logger_main(void)
{
  int facility = LOG_USER, priority = LOG_NOTICE;
  char *message = NULL;

  if (toys.optflags & FLAG_p) {
    char *sep = strchr(TT.priority_arg, '.');

    if (sep) {
      *sep = '\0';
      if ((facility = lookup(facilities, TT.priority_arg)) == -1)
        error_exit("bad facility: %s", TT.priority_arg);
      TT.priority_arg = sep+1;
    }

    if ((priority = lookup(priorities, TT.priority_arg)) == -1)
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
