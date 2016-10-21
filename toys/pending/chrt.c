/* chrt.c - Get/set real-time (scheduling) attributes
 *
 * Copyright 2016 The Android Open Source Project

USE_CHRT(NEWTOY(chrt, "mp#bfiorR[!bfior]", TOYFLAG_USR|TOYFLAG_SBIN))

config CHRT
  bool "chrt"
  default n
  help
    usage: chrt [-m] [-p PID] [POLICY PRIO] [COMMAND [ARGS...]]

    Get/set a process' real-time (scheduling) attributes.

    -p	Apply to given pid
    -R	Set SCHED_RESET_ON_FORK
    -m	Show min/max priorities available

    Policies:
      -b  SCHED_BATCH    -f  SCHED_FIFO    -i  SCHED_IDLE
      -o  SCHED_OTHER    -r  SCHED_RR
*/

#define FOR_chrt
#include "toys.h"

#include <linux/sched.h>

GLOBALS(
  long pid;
)

static char *policy_name(int policy) {
  char *policy_names[] = { "SCHED_OTHER", "SCHED_FIFO", "SCHED_RR",
    "SCHED_BATCH", "4", "SCHED_IDLE", "SCHED_DEADLINE" };

  return policy < ARRAY_LEN(policy_names) ? policy_names[policy] : "???";
}

void chrt_main(void)
{
  int policy = SCHED_RR;
  struct sched_param p;

  // Show min/maxes?
  if (toys.optflags&FLAG_m) {
    for (policy = SCHED_OTHER; policy <= SCHED_IDLE; ++policy)
      if (policy != 4) // There's an unused hole in the priorities.
        printf("%s min/max priority\t: %d/%d\n", policy_name(policy),
               sched_get_priority_min(policy), sched_get_priority_max(policy));
    return;
  }

  // If we have a pid but no command or policy, we're just querying.
  if (TT.pid && !*(toys.optargs+1) &&
      !(toys.optflags&(FLAG_b|FLAG_f|FLAG_i|FLAG_o|FLAG_r))) {
    policy = sched_getscheduler(TT.pid);
    if (policy == -1) perror_exit("sched_getscheduler");
    policy &= ~SCHED_RESET_ON_FORK;
    printf("pid %ld's current scheduling policy: %s\n",
           TT.pid, policy_name(policy));

    if (sched_getparam(TT.pid, &p)) perror_exit("sched_getparam");
    printf("pid %ld's current scheduling priority: %d\n",
           TT.pid, p.sched_priority);

    return;
  }

  // Did we get a meaningful combination of arguments?
  if (!*toys.optargs) help_exit("missing priority");
  if (TT.pid && *(toys.optargs+1)) help_exit("-p and command");
  if (!TT.pid && !*(toys.optargs+1)) help_exit("missing command");

  // Translate into policy and priority.
  if (toys.optflags&FLAG_b) policy = SCHED_BATCH;
  else if (toys.optflags&FLAG_f) policy = SCHED_FIFO;
  else if (toys.optflags&FLAG_i) policy = SCHED_IDLE;
  else if (toys.optflags&FLAG_o) policy = SCHED_OTHER;

  if (toys.optflags&FLAG_R) policy |= SCHED_RESET_ON_FORK;

  p.sched_priority = atolx_range(*toys.optargs, sched_get_priority_min(policy),
                                 sched_get_priority_max(policy));

  if (sched_setscheduler(TT.pid, policy, &p)) perror_exit("sched_setscheduler");

  if (*(toys.optargs+1)) {
    toys.stacktop = 0;
    xexec(++toys.optargs);
  }
}
