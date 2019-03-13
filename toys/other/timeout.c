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
  struct timeval ktv;
  struct itimerval itv;
)

static void handler(int i)
{
  if (FLAG(v))
    fprintf(stderr, "timeout pid %d signal %d\n", TT.pid, TT.nextsig);

  kill(TT.pid, TT.nextsig);

  if (TT.k) {
    TT.k = 0;
    TT.nextsig = SIGKILL;
    xsignal(SIGALRM, handler);
    TT.itv.it_value = TT.ktv;
    setitimer(ITIMER_REAL, &TT.itv, (void *)toybuf);
  }
}

// timeval inexplicably makes up a new type for microseconds, despite timespec's
// nanoseconds field (needing to store 1000* the range) using "long". Bravo.
void xparsetimeval(char *s, struct timeval *tv)
{
  long ll;

  tv->tv_sec = xparsetime(s, 6, &ll);
  tv->tv_usec = ll;
}

void timeout_main(void)
{
  toys.exitval = 125;

  // Parse early to get any errors out of the way.
  xparsetimeval(*toys.optargs, &TT.itv.it_value);
  if (TT.k) xparsetimeval(TT.k, &TT.ktv);

  TT.nextsig = SIGTERM;
  if (TT.s && -1 == (TT.nextsig = sig_to_num(TT.s)))
    error_exit("bad -s: '%s'", TT.s);

  if (!FLAG(foreground)) setpgid(0, 0);

  if (!(TT.pid = XVFORK())) xexec(toys.optargs+1);
  else {
    int status;

    xsignal(SIGALRM, handler);
    setitimer(ITIMER_REAL, &TT.itv, (void *)toybuf);

    while (-1 == waitpid(TT.pid, &status, 0) && errno == EINTR);
    if (WIFEXITED(status)) toys.exitval = WEXITSTATUS(status);
    else if (WTERMSIG(status)==SIGKILL) toys.exitval = 137;
    else toys.exitval = FLAG(preserve_status) ? 128+WTERMSIG(status) : 124;
  }
}
