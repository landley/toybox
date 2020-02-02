/* time.c - time a simple command
 *
 * Copyright 2013 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/time.html

USE_TIME(NEWTOY(time, "<1^pv", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_MAYFORK))

config TIME
  bool "time"
  default y
  depends on TOYBOX_FLOAT
  help
    usage: time [-pv] COMMAND...

    Run command line and report real, user, and system time elapsed in seconds.
    (real = clock on the wall, user = cpu used by command's code,
    system = cpu used by OS on behalf of command.)

    -p	POSIX format output (default)
    -v	Verbose
*/

#define FOR_time
#include "toys.h"

void time_main(void)
{
  pid_t pid;
  struct timeval tv, tv2;

  gettimeofday(&tv, NULL);
  if (!(pid = XVFORK())) xexec(toys.optargs);
  else {
    int stat;
    struct rusage ru;
    float r, u, s;

    wait4(pid, &stat, 0, &ru);
    gettimeofday(&tv2, NULL);
    if (tv.tv_usec > tv2.tv_usec) {
      tv2.tv_usec += 1000000;
      tv2.tv_sec--;
    }
    r = (tv2.tv_sec-tv.tv_sec)+((tv2.tv_usec-tv.tv_usec)/1000000.0);
    u = ru.ru_utime.tv_sec+(ru.ru_utime.tv_usec/1000000.0);
    s = ru.ru_stime.tv_sec+(ru.ru_stime.tv_usec/1000000.0);
    if (FLAG(v)) fprintf(stderr, "Real time (s): %f\nSystem time (s): %f\n"
      "User time (s): %f\nMax RSS (KiB): %ld\nMajor faults: %ld\n"
      "Minor faults: %ld\nFile system inputs: %ld\nFile system outputs: %ld\n"
      "Voluntary context switches: %ld\nInvoluntary context switches: %ld\n",
      r, s, u, ru.ru_maxrss, ru.ru_majflt, ru.ru_minflt, ru.ru_inblock,
      ru.ru_oublock, ru.ru_nvcsw, ru.ru_nivcsw);
    else fprintf(stderr, "real %f\nuser %f\nsys %f\n", r, u, s);
    toys.exitval = WIFEXITED(stat) ? WEXITSTATUS(stat) : WTERMSIG(stat);
  }
}
