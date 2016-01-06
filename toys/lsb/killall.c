/* killall.c - Send signal (default: TERM) to all processes with given names.
 *
 * Copyright 2012 Andreas Heck <aheck@gmx.de>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/killall.html

USE_KILLALL(NEWTOY(killall, "?s:lqvi", TOYFLAG_USR|TOYFLAG_BIN))

config KILLALL
  bool "killall"
  default y
  help
    usage: killall [-l] [-iqv] [-SIGNAL|-s SIGNAL] PROCESS_NAME...

    Send a signal (default: TERM) to all processes with the given names.

    -i	ask for confirmation before killing
    -l	print list of all available signals
    -q	don't print any warnings or error messages
    -s	send SIGNAL instead of SIGTERM
    -v	report if the signal was successfully sent
*/

#define FOR_killall
#include "toys.h"

GLOBALS(
  char *sig;

  int signum;
  pid_t cur_pid;
  char **names;
  short *err;
)

static int kill_process(pid_t pid, char *name)
{
  int offset = 0;

  if (pid == TT.cur_pid) return 0;

  if (toys.optflags & FLAG_i) {
    fprintf(stderr, "Signal %s(%d)", name, (int)pid);
    if (!yesno(0)) return 0;
  }

  errno = 0;
  kill(pid, TT.signum);
  for (;;) {
    if (TT.names[offset] == name) {
      TT.err[offset] = errno;
      break;
    } else offset++;
  }
  if (errno) {
    if (!(toys.optflags & FLAG_q)) perror_msg("pid %d", (int)pid);
  } else if (toys.optflags & FLAG_v)
    printf("Killed %s(%d) with signal %d\n", name, pid, TT.signum);

  return 0;
}

void killall_main(void)
{
  int i;

  TT.names = toys.optargs;
  TT.signum = SIGTERM;

  if (toys.optflags & FLAG_l) {
    sig_to_num(NULL);
    return;
  }

  if (TT.sig || (*TT.names && **TT.names == '-')) {
    if (0 > (TT.signum = sig_to_num(TT.sig ? TT.sig : (*TT.names)+1))) {
      if (toys.optflags & FLAG_q) exit(1);
      error_exit("Invalid signal");
    }
    if (!TT.sig) {
      TT.names++;
      toys.optc--;
    }
  }

  if (!(toys.optflags & FLAG_l) && !toys.optc) help_exit("no name");

  TT.cur_pid = getpid();

  TT.err = xmalloc(2*toys.optc);
  for (i=0; i<toys.optc; i++) TT.err[i] = ESRCH;
  names_to_pid(TT.names, kill_process);
  for (i=0; i<toys.optc; i++) {
    if (TT.err[i]) {
      toys.exitval = 1;
      errno = TT.err[i];
      perror_msg_raw(TT.names[i]);
    }
  }
  if (CFG_TOYBOX_FREE) free(TT.err);
}
