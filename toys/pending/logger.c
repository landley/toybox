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
#include <strings.h>
#include <string.h>

GLOBALS(
  char *priority_arg;
  char *ident;

  int facility;
  int priority;
)

struct mapping {
  const char *key;
  int value;
};

static const struct mapping facilities[] = {
  {"user", LOG_USER}, {"main", LOG_MAIL}, {"news", LOG_NEWS},
  {"uucp", LOG_UUCP}, {"daemon", LOG_DAEMON}, {"auth", LOG_AUTH},
  {"cron", LOG_CRON}, {"lpr", LOG_LPR}, {"local0", LOG_LOCAL0},
  {"local1", LOG_LOCAL1}, {"local2", LOG_LOCAL2}, {"local3", LOG_LOCAL3},
  {"local4", LOG_LOCAL4}, {"local5", LOG_LOCAL5}, {"local6", LOG_LOCAL6},
  {"local7", LOG_LOCAL7},
  {NULL, 0}
};

static const struct mapping priorities[] = {
  {"emerg", LOG_EMERG}, {"alert", LOG_ALERT}, {"crit", LOG_CRIT},
  {"err", LOG_ERR}, {"warning", LOG_WARNING}, {"notice", LOG_NOTICE},
  {"info", LOG_INFO}, {"debug", LOG_DEBUG},
  {NULL, 0}
};

static int lookup(const struct mapping *where, const char *key)
{
  int i;
  for (i = 0; where[i].key; i++)
    if (!strcasecmp(key, where[i].key))
      return where[i].value;

  return -1;
}

static void parse_priority()
{
  char *sep = strchr(TT.priority_arg, '.');

  if (sep)
  {
    *sep = '\0';
    if ((TT.facility = lookup(facilities, TT.priority_arg)) == -1)
      error_exit("bad facility: %s", TT.priority_arg);
    TT.priority_arg = sep+1;
  }

  if ((TT.priority = lookup(priorities, TT.priority_arg)) == -1)
    error_exit("bad priority: %s", TT.priority_arg);
}

void logger_main(void)
{
  if (toys.optflags & FLAG_p)
    parse_priority();
  else
  {
    TT.facility = LOG_USER;
    TT.priority = LOG_NOTICE;
  }

  if (!(toys.optflags & FLAG_t))
  {
    struct passwd *pw = getpwuid(geteuid());
    if (!pw)
      perror_exit("getpwuid");
    TT.ident = xstrdup(pw->pw_name);
  }

  char *message = NULL;
  if (toys.optc) {
    int length = 0;
    int pos = 0;

    for (;*toys.optargs; (void) *(toys.optargs)++) // shut up gcc
    {
      length += strlen(*(toys.optargs)) + 1; // plus one for the args spacing
      message = xrealloc(message, length + 1); // another one for the null byte

      sprintf(message + pos, "%s ", *toys.optargs);
      pos = length;
    }
  } else {
    toybuf[readall(0, toybuf, 4096-1)] = '\0';
    message = toybuf;
  }

  openlog(TT.ident, (toys.optflags & FLAG_s ? LOG_PERROR : 0) , TT.facility);
  syslog(TT.priority, "%s", message);
  closelog();
}
