/* nohup.c - run commandline with SIGHUP blocked.
 *
 * Copyright 2011 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/nohup.html

USE_NOHUP(NEWTOY(nohup, "<1^", TOYFLAG_USR|TOYFLAG_BIN))

config NOHUP
  bool "nohup"
  default y
  help
    usage: nohup COMMAND [ARGS...]

    Run a command that survives the end of its terminal.

    Redirect tty on stdin to /dev/null, tty on stdout to "nohup.out".
*/

#include "toys.h"

void nohup_main(void)
{
  xsignal(SIGHUP, SIG_IGN);
  if (isatty(1)) {
    close(1);
    if (-1 == open("nohup.out", O_CREAT|O_APPEND|O_WRONLY,
        S_IRUSR|S_IWUSR ))
    {
      char *temp = getenv("HOME");

      temp = xmprintf("%s/%s", temp ? temp : "", "nohup.out");
      xcreate(temp, O_CREAT|O_APPEND|O_WRONLY, 0600);
      free(temp);
    }
  }
  if (isatty(0)) {
    close(0);
    xopen_stdio("/dev/null", O_RDONLY);
  }
  xexec(toys.optargs);
}
