/* killall5.c - Send signal (default: TERM) to all processes outside current session.
 *
 * Copyright 2014 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2014 Kyungwan Han <asura321@gamil.com>
 *
 * No Standard

USE_KILLALL5(NEWTOY(killall5, "?o*ls:[!lo]", TOYFLAG_SBIN))

config KILLALL5
  bool "killall5"
  default n
  help
    usage: killall5 [-l] [-SIGNAL|-s SIGNAL] [-o PID]...

    Send a signal (default: TERM) to all processes outside current session.

    -l     List all signal names and numbers
    -o PID Don't signal this PID
    -s     send SIGNAL instead of SIGTERM
*/
#define FOR_killall5
#include "toys.h"

GLOBALS(
  char *signame;
  struct arg_list *olist;
)

void killall5_main(void)
{
  DIR *dp;
  struct dirent *entry;
  int signo, pid, sid, signum = SIGTERM;
  long *olist = NULL, ocount = 0;
  char *s, **args = toys.optargs;

  // list all signal names and numbers
  if (toys.optflags & FLAG_l) {
    if (*args) {
      for (; *args; args++) {
    	signo = sig_to_num(*args);
    	if (isdigit(**args) && (s = num_to_sig(signo&127))) xputs(s);
    	else if (signo > 0) xprintf("%d\n", signo);
    	else error_exit("UNKNOWN signal '%s'", *args);
      }
    } else sig_to_num(NULL);
    return;
  }

  // when SIGNUM will be in the form of -SIGNUM
  if (TT.signame || (*args && **args == '-')) {
    if (0 > (signum = sig_to_num(TT.signame ? TT.signame : (*args)+1)))
      error_exit("Unknown signal '%s'", *args);
    if (!TT.signame) args++;
  }
  pid = getpid();
  sid = getsid(pid);

  // prepare omit list
  if (toys.optflags & FLAG_o) {
    struct arg_list *ptr = TT.olist;

    if (*args) error_exit("invalid omit list");
    for (; ptr; ptr=ptr->next) {
      long val = atolx(ptr->arg);
      olist = xrealloc(olist, (ocount+1)*sizeof(long));
      olist[ocount++] = val;
    }
  }

  if (!(dp = opendir("/proc"))) perror_exit("opendir");
  while ((entry = readdir(dp))) {
    int count, procpid, procsid;

    if (!(procpid = atoi(entry->d_name))) continue;
    snprintf(toybuf, sizeof(toybuf), "/proc/%d/stat", procpid);
    if (!readfile(toybuf, toybuf, sizeof(toybuf))) continue;
    if (sscanf(toybuf, "%*d %*s %*c %*d %*d %d", &procsid) != 1) continue;
    if (pid == procpid || sid == procsid || procpid == 1) continue;

    // Check for kernel threads.
    snprintf(toybuf, sizeof(toybuf), "/proc/%d/cmdline", procpid);
    if (readfile(toybuf, toybuf, sizeof(toybuf)) && !*toybuf) continue;

    // Check with omit list.
    if (toys.optflags & FLAG_o) {
      for (count = 0; count < ocount; count++) {
        if (procpid == olist[count]) goto OMIT;
      }
    }    
    kill(procpid, signum);
OMIT: ;
  }
  closedir(dp);
  if (CFG_TOYBOX_FREE && olist) free(olist);
}
