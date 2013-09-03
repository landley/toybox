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
    usage: killall [-l] [-iqv] [-SIG] PROCESS_NAME...

    Send a signal (default: TERM) to all processes with the given names.

    -i	ask for confirmation before killing
    -l	print list of all available signals
    -q	don't print any warnings or error messages
    -v	report if the signal was successfully sent
*/

#define FOR_killall
#include "toys.h"

GLOBALS(
  int signum;
  pid_t cur_pid;
)

static int kill_process(pid_t pid, char *name)
{
  int ret;

  if (pid == TT.cur_pid) return 0;

  if (toys.optflags & FLAG_i) {
    sprintf(toybuf, "Signal %s(%d) ?", name, pid);
    if (yesno(toybuf, 0) == 0) return 0;
  }

  ret = kill(pid, TT.signum);
  if (ret == -1 && !(toys.optflags & FLAG_q))
    error_msg("bad %u", (unsigned)pid);
  else if (toys.optflags & FLAG_v)
    printf("Killed %s(%d) with signal %d\n", name, pid, TT.signum);

  return 0;
}

void killall_main(void)
{
  char **names = toys.optargs;

  TT.signum = SIGTERM;
  toys.exitval++;

  if (toys.optflags & FLAG_l) {
    sig_to_num(NULL);
    return;
  }

  if (**names == '-') {
    if (0 > (TT.signum = sig_to_num((*names)+1))) {
      if (toys.optflags & FLAG_q) exit(1);
      error_exit("Invalid signal");
    }
    names++;
  }

  if (!*names) {
    toys.exithelp++;
    error_exit("Process name missing!");
  }

  TT.cur_pid = getpid();

  names_to_pid(names, kill_process);

  if (toys.exitval && !(toys.optflags & FLAG_q)) error_exit("No such process");
}
