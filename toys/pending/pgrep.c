/* pgrep.c - pgrep and pkill implementation
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *

USE_PGREP(NEWTOY(pgrep, "?P# s# xvonlf[!sP]", TOYFLAG_USR|TOYFLAG_BIN))
USE_PGREP(OLDTOY(pkill, pgrep, TOYFLAG_USR|TOYFLAG_BIN))

config PGREP
  bool "pgrep"
  default n
  help
    usage: pgrep [-flnovx] [-s SID|-P PPID|PATTERN]
           pkill [-l|-SIGNAL] [-fnovx] [-s SID|-P PPID|PATTERN]

    -l  Show command name too / List all signals
    -f  Match against entire command line
    -n  Show/Signal the newest process only
    -o  Show/Signal the oldest process only
    -v  Negate the match
    -x  Match whole name (not substring)
    -s  Match session ID (0 for current)
    -P  Match parent process ID
*/

#define FOR_pgrep
#include "toys.h"

GLOBALS(
  long sid;       //-s
  long ppid;      //-P

  char *signame;
)

static int exec_action(unsigned pid, char *name, int signal)
{
  if (toys.which->name[1] == 'g') {
    printf("%d", pid);
    if (toys.optflags&FLAG_l) printf(" %s", name);
    printf("\n");
  } else kill(pid, signal);

  return 0;
}

static int regex_match(regex_t *rp, char *tar, char *patt)
{
  regmatch_t rm[1];
  int len = strlen(tar);
  if (regexec(rp, tar, 1, rm, 0) == 0) {
    if (toys.optflags&FLAG_x) {
      if ((rm[0].rm_so == 0) && ((rm[0].rm_eo - rm[0].rm_so) == len)) return 1;
    } else return 1;
  }
  return 0;
}

void pgrep_main(void)
{
  int signum = 0, eval = 0, ret = 1;
  DIR *dp = 0;
  struct dirent *entry = 0;
  regex_t rp;
  unsigned pid = 0, ppid = 0, sid = 0, latest_pid = 0;
  char *cmdline = 0, *latest_cmdline = 0;
  pid_t self = getpid();

  if (!(dp = opendir("/proc"))) perror_exit("OPENDIR: failed to open /proc");
  setlinebuf(stdout);

  // pkill
  if (toys.which->name[1] == 'k') {
    if (toys.optflags&FLAG_l) {
      sig_to_num(0);

      return;
    }
// note: "pkill -" on ubuntu uses "-" as the pattern, this would error instead
    if (*toys.optargs && **toys.optargs == '-') {
      char *arg;
      int i = strtol(TT.signame = *(toys.optargs++)+1, &arg, 10);

      if (!*arg) arg = num_to_sig(i);
      else arg = TT.signame;
      if (!arg || (signum = sig_to_num(arg)) == -1)
        error_exit("Unknown signal '%s'", arg);
    } else signum = SIGTERM;
  }
  if (!(toys.optflags&(FLAG_s|FLAG_P)) && !*toys.optargs)
    help_exit("missing argument");
  if (toys.optargs[1] && !(toys.optflags&(FLAG_s|FLAG_P)))
    help_exit("max argument > 1");
  if (*toys.optargs) { /* compile regular expression(PATTERN) */
    if ((eval = regcomp(&rp, *toys.optargs, REG_EXTENDED | REG_NOSUB)) != 0) {
      char errbuf[256];

      (void) regerror(eval, &rp, errbuf, sizeof(errbuf));
      error_exit("%s", errbuf);
    }
  }
  if ((toys.optflags&FLAG_s) && !TT.sid) TT.sid = getsid(0);
  while ((entry = readdir(dp))) {
    int fd = -1, n = 0;
    if (!isdigit(*entry->d_name)) continue;

    pid = strtol(entry->d_name, NULL, 10);
    if (pid == self) continue;

    snprintf(toybuf, sizeof(toybuf), "/proc/%s/cmdline", entry->d_name);
    if ((fd = open(toybuf, O_RDONLY)) == -1) goto cmdline_fail;
    n = read(fd, toybuf, sizeof(toybuf));
    close(fd);
    toybuf[n--] = '\0';
    if (n < 0) {
cmdline_fail:
      snprintf(toybuf, sizeof(toybuf), "/proc/%s/comm", entry->d_name);
      if ((fd = open(toybuf, O_RDONLY)) == -1) continue;
      n = read(fd, toybuf, sizeof(toybuf));
      close(fd);
      toybuf[--n] = '\0';
      if (n < 1) continue;
    }
    if (toys.optflags & FLAG_f) {
      while (--n)
        if (toybuf[n] < ' ') toybuf[n] = ' ';
    }
    if (cmdline) free(cmdline);
    cmdline = xstrdup(toybuf);
    if (toys.optflags&(FLAG_s|FLAG_P)) {
      snprintf(toybuf, sizeof(toybuf), "/proc/%s/stat", entry->d_name);
      if ((fd = open(toybuf, O_RDONLY)) == -1) continue;
      n = read(fd, toybuf, sizeof(toybuf));
      close(fd);
      if (n<1) continue;
      n = sscanf(toybuf, "%*u %*s %*c %u %*u %u", &ppid, &sid);
      if ((toys.optflags&FLAG_s) && sid != TT.sid) continue;
      if ((toys.optflags&FLAG_P) && ppid != TT.ppid) continue;
    }
    if (!*toys.optargs || (regex_match(&rp, cmdline, *toys.optargs)^!!(toys.optflags&FLAG_v))) {
      if (toys.optflags&FLAG_n) {
        if (latest_cmdline) free(latest_cmdline);
        latest_cmdline = xstrdup(cmdline);
        latest_pid = pid;
      } else exec_action(pid, cmdline, signum);
      ret = 0;
      if (toys.optflags&FLAG_o) break;
    }
  }
  if (cmdline) free(cmdline);
  if (latest_cmdline) {
    exec_action(latest_pid, latest_cmdline, signum);
    free(latest_cmdline);
  }
  if (*toys.optargs) regfree(&rp);
  closedir(dp);
  toys.exitval = ret;
}
