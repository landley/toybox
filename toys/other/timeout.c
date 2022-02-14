/* timeout.c - Run command line with a timeout
 *
 * Copyright 2013 Rob Landley <rob@landley.net>
 *
 * No standard

USE_TIMEOUT(NEWTOY(timeout, "<2^(foreground)(preserve-status)vk:s(signal):", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_ARGFAIL(125)))

config TIMEOUT
  bool "timeout"
  default y
  help
    usage: timeout [-k DURATION] [-s SIGNAL] DURATION COMMAND...

    Run command line as a child process, sending child a signal if the
    command doesn't exit soon enough.

    DURATION can be a decimal fraction. An optional suffix can be "m"
    (minutes), "h" (hours), "d" (days), or "s" (seconds, the default).

    -s	Send specified signal (default TERM)
    -k	Send KILL signal if child still running this long after first signal
    -v	Verbose
    --foreground       Don't create new process group
    --preserve-status  Exit with the child's exit status
*/

#define FOR_timeout
#include "toys.h"

GLOBALS(
  char *s, *k;

  int nextsig;
  pid_t pid;
  struct timespec kts;
  struct itimerspec its;
  timer_t timer;
)

static void handler(int i)
{
  if (FLAG(v))
    fprintf(stderr, "timeout pid %d signal %d\n", TT.pid, TT.nextsig);

  toys.exitval = (TT.nextsig==9) ? 137 : 124;
  kill(TT.pid, TT.nextsig);
  if (TT.k) {
    TT.k = 0;
    TT.nextsig = SIGKILL;
    xsignal(SIGALRM, handler);
    TT.its.it_value = TT.kts;
    if (timer_settime(TT.timer, 0, &TT.its, 0)) perror_exit("timer_settime");
  }
}

void timeout_main(void)
{
  struct sigevent se = { .sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIGALRM };

  // Use same ARGFAIL value for any remaining parsing errors
  toys.exitval = 125;
  xparsetimespec(*toys.optargs, &TT.its.it_value);
  if (TT.k) xparsetimespec(TT.k, &TT.kts);

  TT.nextsig = SIGTERM;
  if (TT.s && -1 == (TT.nextsig = sig_to_num(TT.s)))
    error_exit("bad -s: '%s'", TT.s);

  if (!FLAG(foreground)) setpgid(0, 0);

  toys.exitval = 0;
  if (!(TT.pid = XVFORK())) xexec(toys.optargs+1);
  else {
    int status;

    xsignal(SIGALRM, handler);
    if (timer_create(CLOCK_MONOTONIC, &se, &TT.timer)) perror_exit("timer");
    if (timer_settime(TT.timer, 0, &TT.its, 0)) perror_exit("timer_settime");

    status = xwaitpid(TT.pid);
    if (FLAG(preserve_status) || !toys.exitval) toys.exitval = status;
  }
}
