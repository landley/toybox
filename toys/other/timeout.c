/* timeout.c - Run command line with a timeout
 *
 * Copyright 2013 Rob Landley <rob@landley.net>
 *
 * No standard

USE_TIMEOUT(NEWTOY(timeout, "<2^(foreground)(preserve-status)vk:s(signal):i", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_ARGFAIL(125)))

config TIMEOUT
  bool "timeout"
  default y
  help
    usage: timeout [-i] [-k DURATION] [-s SIGNAL] DURATION COMMAND...

    Run command line as a child process, sending child a signal if the
    command doesn't exit soon enough.

    DURATION can be a decimal fraction. An optional suffix can be "m"
    (minutes), "h" (hours), "d" (days), or "s" (seconds, the default).

    -i	Only kill for inactivity (restart timeout when command produces output)
    -k	Send KILL signal if child still running this long after first signal
    -s	Send specified signal (default TERM)
    -v	Verbose
    --foreground       Don't create new process group
    --preserve-status  Exit with the child's exit status
*/

#define FOR_timeout
#include "toys.h"

GLOBALS(
  char *s, *k;

  struct pollfd pfd;
  sigjmp_buf sj;
)

static void handler(int sig)
{
  siglongjmp(TT.sj, 1);
}

static long nantomil(struct timespec *ts)
{
  return ts->tv_sec*1000+ts->tv_nsec/1000000;
}

void timeout_main(void)
{
  int fds[] = {0, -1}, ii, ms, nextsig, pid;
  struct timespec tts, kts;

  // Use same ARGFAIL value for any remaining parsing errors
  toys.exitval = 125;
  xparsetimespec(*toys.optargs, &tts);
  if (TT.k) xparsetimespec(TT.k, &kts);

  nextsig = SIGTERM;
  if (TT.s && -1 == (nextsig = sig_to_num(TT.s)))
    error_exit("bad -s: '%s'", TT.s);

  if (!FLAG(foreground)) setpgid(0, 0);

  toys.exitval = 0;
  TT.pfd.events = POLLIN;
  if (sigsetjmp(TT.sj, 1)) goto done;
  xsignal_flags(SIGCHLD, handler, SA_NOCLDSTOP);
  pid = xpopen_both(toys.optargs+1, FLAG(i) ? fds : 0);
  if (!FLAG(i)) xpipe(fds);
  TT.pfd.fd = fds[1];
  ms = nantomil(&tts);
  for (;;) {
    if (1 != xpoll(&TT.pfd, 1, ms)) {
      if (FLAG(v))
        perror_msg("sending signal %s to command %s", num_to_sig(nextsig),
          toys.optargs[1]);
      toys.exitval = (nextsig==9) ? 137 : 124;
      kill(pid, nextsig);
      if (!TT.k || nextsig==SIGKILL) break;
      nextsig = SIGKILL;
      ms = nantomil(&kts);

      continue;
    }
    if (TT.pfd.revents&POLLIN) {
      errno = 0;
      if (1>(ii = read(fds[1], toybuf, sizeof(toybuf)))) {
        if (errno==EINTR) continue;
        break;
      }
      writeall(1, toybuf, ii);
    }
    if (TT.pfd.revents&POLLHUP) break;
  }
done:
  xsignal(SIGCHLD, SIG_DFL);
  ii = xpclose_both(pid, fds);

  if (FLAG(preserve_status) || !toys.exitval) toys.exitval = ii;
}
