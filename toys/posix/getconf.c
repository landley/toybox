/* getconf.c - get configuration values
 *
 * Copyright 2017 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/getconf.c
 *
 * Deviations from posix: no -v because nothing says what it should DO.

USE_GETCONF(NEWTOY(getconf, "l", TOYFLAG_USR|TOYFLAG_BIN))

config GETCONF
  bool "getconf"
  default y
  help
    usage: getconf [-l] [NAME...]

    Get system configuration values.

    -l	List available value names
*/

#define FOR_getconf
#include "toys.h"
#include <limits.h>

// make.sh calls sed on this file (getconf.c) to extract symbols from our three
// XXX_names arrays and matches them with #defines in limits.h and unistd.h to
// produce getconf.h, which contains corresponding XXX_values[] arrays
// with the appropriate constants or -1 for unknown entries.

#define UNKNOWN -1
#include "generated/getconf.h"

// Lists of symbols getconf can query, broken down by whether we call sysconf(),
// getconf(), or output the macro value directly.

// Probe the live system
char *sysconf_names[] = {
  /* POSIX */
  "_POSIX_ADVISORY_INFO", "_POSIX_BARRIERS", "_POSIX_ASYNCHRONOUS_IO",
  "_POSIX_CLOCK_SELECTION", "_POSIX_CPUTIME", "_POSIX_FSYNC", "_POSIX_IPV6",
  "_POSIX_JOB_CONTROL", "_POSIX_MAPPED_FILES", "_POSIX_MEMLOCK",
  "_POSIX_MEMLOCK_RANGE", "_POSIX_MEMORY_PROTECTION", "_POSIX_MESSAGE_PASSING",
  "_POSIX_MONOTONIC_CLOCK", "_POSIX_PRIORITIZED_IO",
  "_POSIX_PRIORITY_SCHEDULING", "_POSIX_RAW_SOCKETS",
  "_POSIX_READER_WRITER_LOCKS", "_POSIX_REALTIME_SIGNALS", "_POSIX_REGEXP",
  "_POSIX_SAVED_IDS", "_POSIX_SEMAPHORES", "_POSIX_SHARED_MEMORY_OBJECTS",
  "_POSIX_SHELL", "_POSIX_SPAWN", "_POSIX_SPIN_LOCKS", "_POSIX_SPORADIC_SERVER",
  "_POSIX_SS_REPL_MAX", "_POSIX_SYNCHRONIZED_IO",
  "_POSIX_THREAD_ATTR_STACKADDR", "_POSIX_THREAD_ATTR_STACKSIZE",
  "_POSIX_THREAD_CPUTIME", "_POSIX_THREAD_PRIO_INHERIT",
  "_POSIX_THREAD_PRIO_PROTECT", "_POSIX_THREAD_PRIORITY_SCHEDULING",
  "_POSIX_THREAD_PROCESS_SHARED", "_POSIX_THREAD_ROBUST_PRIO_INHERIT",
  "_POSIX_THREAD_ROBUST_PRIO_PROTECT", "_POSIX_THREAD_SAFE_FUNCTIONS",
  "_POSIX_THREAD_SPORADIC_SERVER", "_POSIX_THREADS", "_POSIX_TIMEOUTS",
  "_POSIX_TIMERS", "_POSIX_TRACE", "_POSIX_TRACE_EVENT_FILTER",
  "_POSIX_TRACE_EVENT_NAME_MAX", "_POSIX_TRACE_INHERIT", "_POSIX_TRACE_LOG",
  "_POSIX_TRACE_NAME_MAX", "_POSIX_TRACE_SYS_MAX",
  "_POSIX_TRACE_USER_EVENT_MAX", "_POSIX_TYPED_MEMORY_OBJECTS",
  "_POSIX_VERSION", "_POSIX_V7_ILP32_OFF32", "_POSIX_V7_ILP32_OFFBIG",
  "_POSIX_V7_LP64_OFF64", "_POSIX_V7_LPBIG_OFFBIG",

  /* POSIX.2 */
  "_POSIX2_C_BIND", "_POSIX2_C_DEV", "_POSIX2_CHAR_TERM", "_POSIX2_FORT_DEV",
  "_POSIX2_FORT_RUN", "_POSIX2_LOCALEDEF", "_POSIX2_PBS",
  "_POSIX2_PBS_ACCOUNTING", "_POSIX2_PBS_CHECKPOINT", "_POSIX2_PBS_LOCATE",
  "_POSIX2_PBS_MESSAGE", "_POSIX2_PBS_TRACK", "_POSIX2_SW_DEV", "_POSIX2_UPE",
  "_POSIX2_VERSION",

  /* X/Open */
  "_XOPEN_CRYPT", "_XOPEN_ENH_I18N", "_XOPEN_REALTIME",
  "_XOPEN_REALTIME_THREADS", "_XOPEN_SHM", "_XOPEN_STREAMS", "_XOPEN_UNIX",
  "_XOPEN_UUCP", "_XOPEN_VERSION",

  /* No obvious standard */
  "AIO_LISTIO_MAX", "AIO_MAX", "AIO_PRIO_DELTA_MAX", "ARG_MAX", "ATEXIT_MAX",
  "BC_BASE_MAX", "BC_DIM_MAX", "BC_SCALE_MAX", "BC_STRING_MAX", "CHILD_MAX",
  "COLL_WEIGHTS_MAX", "DELAYTIMER_MAX", "EXPR_NEST_MAX", "HOST_NAME_MAX",
  "IOV_MAX", "LINE_MAX", "LOGIN_NAME_MAX", "NGROUPS_MAX", "MQ_OPEN_MAX",
  "MQ_PRIO_MAX", "OPEN_MAX", "PAGE_SIZE", "PAGESIZE",
  "PTHREAD_DESTRUCTOR_ITERATIONS", "PTHREAD_KEYS_MAX", "PTHREAD_STACK_MIN",
  "PTHREAD_THREADS_MAX", "RE_DUP_MAX", "RTSIG_MAX", "SEM_NSEMS_MAX",
  "SEM_VALUE_MAX", "SIGQUEUE_MAX", "STREAM_MAX", "SYMLOOP_MAX", "TIMER_MAX",
  "TTY_NAME_MAX", "TZNAME_MAX", "NPROCESSORS_CONF", "NPROCESSORS_ONLN"
};

// Strings out of a header
char *confstr_names[] = {
  "PATH", "V7_ENV"
};

// Integers out of a header
char *limit_names[] = {
  "_POSIX_AIO_LISTIO_MAX", "_POSIX_AIO_MAX", "_POSIX_ARG_MAX",
  "_POSIX_CHILD_MAX", "_POSIX_DELAYTIMER_MAX", "_POSIX_HOST_NAME_MAX",
  "_POSIX_LINK_MAX", "_POSIX_LOGIN_NAME_MAX", "_POSIX_MAX_CANON",
  "_POSIX_MAX_INPUT", "_POSIX_NAME_MAX", "_POSIX_NGROUPS_MAX",
  "_POSIX_OPEN_MAX", "_POSIX_PATH_MAX", "_POSIX_PIPE_BUF", "_POSIX_RE_DUP_MAX",
  "_POSIX_RTSIG_MAX", "_POSIX_SEM_NSEMS_MAX", "_POSIX_SEM_VALUE_MAX",
  "_POSIX_SIGQUEUE_MAX", "_POSIX_SSIZE_MAX", "_POSIX_STREAM_MAX",
  "_POSIX_SYMLINK_MAX", "_POSIX_SYMLOOP_MAX",
  "_POSIX_THREAD_DESTRUCTOR_ITERATIONS", "_POSIX_THREAD_KEYS_MAX",
  "_POSIX_THREAD_THREADS_MAX", "_POSIX_TIMER_MAX", "_POSIX_TTY_NAME_MAX",
  "_POSIX_TZNAME_MAX", "_POSIX2_BC_BASE_MAX", "_POSIX2_BC_DIM_MAX",
  "_POSIX2_BC_SCALE_MAX", "_POSIX2_BC_STRING_MAX", "_POSIX2_CHARCLASS_NAME_MAX",
  "_POSIX2_COLL_WEIGHTS_MAX", "_POSIX2_EXPR_NEST_MAX", "_POSIX2_LINE_MAX",
  "_POSIX2_RE_DUP_MAX"
};

void getconf_main(void)
{
  char **names[] = {sysconf_names, confstr_names, limit_names},
    **args;
  int i, j, lens[] = {ARRAY_LEN(sysconf_names),
    ARRAY_LEN(confstr_names), ARRAY_LEN(limit_names)};

  if (toys.optflags&FLAG_l) {
    for (i = 0; i<3; i++) for (j = 0; j<lens[i]; j++) puts(names[i][j]);

    return;
  }

  for (args = toys.optargs; *args; args++) {
    char *name = *args;

    // Workaround for autogen using CS_PATH instead of PATH
    if (!strcmp("CS_PATH", name)) name += 3;

    for (i = 0; i<3; i++) for (j = 0; j<lens[i]; j++) {
      if (strcmp(names[i][j], name)) continue;

      if (!i) printf("%ld\n", sysconf(sysconf_vals[j]));
      else if (i==1) {
        confstr(confstr_vals[j], toybuf, sizeof(toybuf));
        puts(toybuf);
      } else printf("%d\n", limit_vals[j]);

      goto cont;
    }
    error_msg("bad '%s'", name);
cont:
    ;
  }
}
