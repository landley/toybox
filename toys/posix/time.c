/* time.c - time a simple command
 *
 * Copyright 2013 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/time.html

USE_TIME(NEWTOY(time, "<1^pv[-pv]", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_MAYFORK))

config TIME
  bool "time"
  default y
  help
    usage: time [-pv] COMMAND...

    Run command line and report real, user, and system time elapsed in seconds.
    (real = clock on the wall, user = cpu used by command's code,
    system = cpu used by OS on behalf of command.)

    -p	POSIX format output
    -v	Verbose
*/

#define FOR_time
#include "toys.h"


void time_main(void)
{
  struct timeval tv, tv2;
  struct rusage ru;
  long long sec[3];
  int stat, ii, idx, nano[3];
  pid_t pid;
  char *label[] = {"\nreal"+!!FLAG(p), "user", "sys"},
       tab = toys.optflags ? ' ' : '\t';

  if (FLAG(v))
    memcpy(label, (char *[]){"Real", "User", "System"}, 3*sizeof(char *));
  gettimeofday(&tv, NULL);
  if (!(pid = XVFORK())) xexec(toys.optargs);
  wait4(pid, &stat, 0, &ru);
  gettimeofday(&tv2, NULL);
  if (tv.tv_usec > tv2.tv_usec) {
    tv2.tv_usec += 1000000;
    tv2.tv_sec--;
  }
  sec[0] = tv2.tv_sec-tv.tv_sec, nano[0] = tv2.tv_usec-tv.tv_usec;
  sec[1] = ru.ru_utime.tv_sec, nano[1] = ru.ru_utime.tv_usec;
  sec[2] = ru.ru_stime.tv_sec, nano[2] = ru.ru_stime.tv_usec;
  for (ii = idx = 0; ii<3; ii++) {
    if (!toys.optflags) sec[ii] = (sec[ii]+500)/1000;
    idx += sprintf(toybuf+idx, "%s%s%c%lld.%0*d\n", label[ii],
                   FLAG(v) ? " time (s):" : "", tab, sec[ii],
                   3<<!!toys.optflags, nano[ii]);
  }
  if (FLAG(v)) idx += sprintf(toybuf+idx,
    "Max RSS (KiB): %ld\nMajor faults: %ld\n"
    "Minor faults: %ld\nFile system inputs: %ld\nFile system outputs: %ld\n"
    "Voluntary context switches: %ld\nInvoluntary context switches: %ld\n",
    ru.ru_maxrss, ru.ru_majflt, ru.ru_minflt, ru.ru_inblock,
    ru.ru_oublock, ru.ru_nvcsw, ru.ru_nivcsw);
  writeall(2, toybuf, idx);

  toys.exitval = WIFEXITED(stat) ? WEXITSTATUS(stat) : WTERMSIG(stat);
}
