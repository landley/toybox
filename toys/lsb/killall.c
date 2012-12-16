
/* killall.c - Send signal (default: TERM) to all processes with given names.
 *
 * Copyright 2012 Andreas Heck <aheck@gmx.de>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/killall.html

USE_KILLALL(NEWTOY(killall, "<1?lqvi", TOYFLAG_USR|TOYFLAG_BIN))

config KILLALL
  bool "killall"
  default y
  help
    usage: killall [-l] [-qv] [-SIG] PROCESS_NAME...

    Send a signal (default: TERM) to all processes with the given names.

    -l	print list of all available signals
    -i	ask for confirmation before killing
    -v	report if the signal was successfully sent
    -q	don't print any warnings or error messages
*/

#define FOR_killall
#include "toys.h"

GLOBALS(
  int signum;
)

static int kill_process(pid_t pid, char *name)
{
  int ret;

  if(toys.optflags & FLAG_i) {
    snprintf(toybuf, sizeof(toybuf), "Signal %s(%d) ?", name, pid);
    if (yesno(toybuf, 0) == 0) return 1;
  }

  toys.exitval = 0;

  ret = kill(pid, TT.signum);
  if (toys.optflags & FLAG_v)
    printf("Killed %s(%d) with signal %d\n", name, pid, TT.signum);

  if (ret == -1 && !(toys.optflags & FLAG_q)) perror("kill");
  return 1;
}

void killall_main(void)
{
  char **names;

  if (toys.optflags & FLAG_l) {
    sig_to_num(NULL);
    return;
  }

  TT.signum = SIGTERM;
  toys.exitval++;

  if (!*toys.optargs) {
    toys.exithelp++;
    error_exit("Process name missing!");
  }

  names = toys.optargs;

  if (**names == '-') {
    if (0 > (TT.signum = sig_to_num((*names)+1))) {
      if (toys.optflags & FLAG_q) exit(1);
      error_exit("Invalid signal");
    }
    names++;

    if (!*names) {
      toys.exithelp++;
      error_exit("Process name missing!");
    }
  }

  for_each_pid_with_name_in(names, kill_process);

  if (toys.exitval && !(toys.optflags & FLAG_q)) error_exit("No such process");
}
