/* kill.c - a program to send signals to processes
 *
 * Copyright 2012 Daniel Walter <d.walter@0x90.at>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/kill.html
 *
 * killall5.c - Send signal to all processes outside current session.
 *
 * Copyright 2014 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2014 Kyungwan Han <asura321@gamil.com>
 *
 * No Standard

USE_KILL(NEWTOY(kill, "?ls: ", TOYFLAG_BIN))
USE_KILLALL5(NEWTOY(killall5, "?o*ls: [!lo][!ls]", TOYFLAG_SBIN))

config KILL
  bool "kill"
  default y
  help
    usage: kill [-l [SIGNAL] | -s SIGNAL | -SIGNAL] pid...

    Send signal to process(es).

    -l	List signal name(s) and number(s)
    -s	Send SIGNAL (default SIGTERM)

config KILLALL5
  bool "killall5"
  default y
  depends on KILL
  help
    usage: killall5 [-l [SIGNAL]] [-SIGNAL|-s SIGNAL] [-o PID]...

    Send a signal to all processes outside current session.

    -l     List signal name(s) and number(s)
    -o PID Omit PID
    -s     send SIGNAL (default SIGTERM)
*/

// This has to match the filename:
#define FOR_kill
#include "toys.h"

GLOBALS(
  char *signame;
  struct arg_list *olist;
)

// But kill's flags are a subset of killall5's

#define CLEANUP_kill
#define FOR_killall5
#include "generated/flags.h"

void kill_main(void)
{
  int signum;
  char *tmp, **args = toys.optargs;
  pid_t pid;

  // list signal(s)
  if (toys.optflags & FLAG_l) {
    if (*args) {
      int signum = sig_to_num(*args);
      char *s = NULL;

      if (signum>=0) s = num_to_sig(signum&127);
      puts(s ? s : "UNKNOWN");
    } else sig_to_num(NULL);
    return;
  }

  // signal must come before pids, so "kill -9 -1" isn't confusing.

  if (!TT.signame && *args && **args=='-') TT.signame=*(args++)+1;
  if (TT.signame) {
    char *arg;
    int i = strtol(TT.signame, &arg, 10);
    if (!*arg) arg = num_to_sig(i);
    else arg = TT.signame;

    if (!arg || -1 == (signum = sig_to_num(arg)))
      error_exit("Unknown signal '%s'", arg);
  } else signum = SIGTERM;

  // is it killall5?
  if (CFG_KILLALL5 && toys.which->name[4]=='a') {
    DIR *dp;
    struct dirent *entry;
    int pid, sid;
    long *olist = 0, ocount = 0;

    // parse omit list
    if (toys.optflags & FLAG_o) {
      struct arg_list *ptr;

      for (ptr = TT.olist; ptr; ptr = ptr->next) ocount++;
      olist = xmalloc(ocount*sizeof(long));
      ocount = 0;
      for (ptr = TT.olist; ptr; ptr=ptr->next)
        olist[ocount++] = atolx(ptr->arg);
    }

    sid = getsid(pid = getpid());

    if (!(dp = opendir("/proc"))) perror_exit("/proc");
    while ((entry = readdir(dp))) {
      int count, procpid, procsid;

      if (!(procpid = atoi(entry->d_name))) continue;

      snprintf(toybuf, sizeof(toybuf), "/proc/%d/stat", procpid);
      if (!readfile(toybuf, toybuf, sizeof(toybuf))) continue;
      if (sscanf(toybuf, "%*d %*s %*c %*d %*d %d", &procsid) != 1) continue;
      if (pid == procpid || sid == procsid || procpid == 1) continue;

      // Check for kernel threads.
      snprintf(toybuf, sizeof(toybuf), "/proc/%d/cmdline", procpid);
      if (!readfile(toybuf, toybuf, sizeof(toybuf)) || !*toybuf) continue;

      // Check with omit list.
      for (count = 0; count < ocount; count++) 
        if (procpid == olist[count]) break;
      if (count != ocount) continue;

      kill(procpid, signum);
    }
    if (CFG_TOYBOX_FREE) {
      closedir(dp);
      free(olist);
    }

  // is it kill?
  } else {

    // "<1" in optstr wouldn't cover this because "-SIGNAL"
    if (!*args) help_exit("missing argument");

    while (*args) {
      char *arg = *(args++);

      pid = strtol(arg, &tmp, 10);
      if (*tmp || kill(pid, signum) < 0) error_msg("unknown pid '%s'", arg);
    }
  }
}

void killall5_main(void)
{
  kill_main();
}
