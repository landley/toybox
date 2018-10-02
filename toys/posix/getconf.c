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

// This is missing on glibc (bionic has it).
#ifndef _SC_XOPEN_UUCP
#define _SC_XOPEN_UUCP -1
#endif

struct config {
  char *name;
  int value;
};

// Lists of symbols getconf can query, broken down by whether we call sysconf(),
// confstr(), or output the macro value directly.

// Probe the live system
struct config sysconfs[] = {
  /* POSIX */
#define CONF(n) {"_POSIX_" #n,_SC_ ## n}
  CONF(ADVISORY_INFO), CONF(BARRIERS), CONF(ASYNCHRONOUS_IO),
  CONF(CLOCK_SELECTION), CONF(CPUTIME), CONF(FSYNC), CONF(IPV6),
  CONF(JOB_CONTROL), CONF(MAPPED_FILES), CONF(MEMLOCK), CONF(MEMLOCK_RANGE),
  CONF(MEMORY_PROTECTION), CONF(MESSAGE_PASSING), CONF(MONOTONIC_CLOCK),
  CONF(PRIORITY_SCHEDULING), CONF(RAW_SOCKETS), CONF(READER_WRITER_LOCKS),
  CONF(REALTIME_SIGNALS), CONF(REGEXP), CONF(SAVED_IDS), CONF(SEMAPHORES),
  CONF(SHARED_MEMORY_OBJECTS), CONF(SHELL), CONF(SPAWN), CONF(SPIN_LOCKS),
  CONF(SPORADIC_SERVER), CONF(SS_REPL_MAX), CONF(SYNCHRONIZED_IO),
  CONF(THREAD_ATTR_STACKADDR), CONF(THREAD_ATTR_STACKSIZE),
  CONF(THREAD_CPUTIME), CONF(THREAD_PRIO_INHERIT), CONF(THREAD_PRIO_PROTECT),
  CONF(THREAD_PRIORITY_SCHEDULING), CONF(THREAD_PROCESS_SHARED),
  CONF(THREAD_ROBUST_PRIO_INHERIT), CONF(THREAD_ROBUST_PRIO_PROTECT),
  CONF(THREAD_SAFE_FUNCTIONS), CONF(THREAD_SPORADIC_SERVER), CONF(THREADS),
  CONF(TIMEOUTS), CONF(TIMERS), CONF(TRACE), CONF(TRACE_EVENT_FILTER),
  CONF(TRACE_EVENT_NAME_MAX), CONF(TRACE_INHERIT), CONF(TRACE_LOG),
  CONF(TRACE_NAME_MAX), CONF(TRACE_SYS_MAX), CONF(TRACE_USER_EVENT_MAX),
  CONF(TYPED_MEMORY_OBJECTS), CONF(VERSION), CONF(V7_ILP32_OFF32),
  CONF(V7_ILP32_OFFBIG), CONF(V7_LP64_OFF64), CONF(V7_LPBIG_OFFBIG),

  /* POSIX.2 */
#undef CONF
#define CONF(n) {"_POSIX2_" #n,_SC_2_ ## n}
  CONF(C_BIND), CONF(C_DEV), CONF(CHAR_TERM), CONF(FORT_DEV), CONF(FORT_RUN),
  CONF(LOCALEDEF), CONF(PBS), CONF(PBS_ACCOUNTING), CONF(PBS_CHECKPOINT),
  CONF(PBS_LOCATE), CONF(PBS_MESSAGE), CONF(PBS_TRACK), CONF(SW_DEV),
  CONF(UPE), CONF(VERSION),

  /* X/Open */
#undef CONF
#define CONF(n) {"_XOPEN_" #n,_SC_XOPEN_ ## n}
  CONF(CRYPT), CONF(ENH_I18N), CONF(REALTIME), CONF(REALTIME_THREADS),
  CONF(SHM), CONF(STREAMS), CONF(UNIX), CONF(UUCP), CONF(VERSION),

  /* No obvious standard */
#undef CONF
#define CONF(n) {#n,_SC_ ## n}
  CONF(AIO_LISTIO_MAX), CONF(AIO_MAX), CONF(AIO_PRIO_DELTA_MAX), CONF(ARG_MAX),
  CONF(ATEXIT_MAX), CONF(BC_BASE_MAX), CONF(BC_DIM_MAX), CONF(BC_SCALE_MAX),
  CONF(BC_STRING_MAX), CONF(CHILD_MAX), CONF(COLL_WEIGHTS_MAX),
  CONF(DELAYTIMER_MAX), CONF(EXPR_NEST_MAX), CONF(HOST_NAME_MAX),
  CONF(IOV_MAX), CONF(LINE_MAX), CONF(LOGIN_NAME_MAX), CONF(NGROUPS_MAX),
  CONF(MQ_OPEN_MAX), CONF(MQ_PRIO_MAX), CONF(OPEN_MAX), CONF(PAGE_SIZE),
  CONF(PAGESIZE),
  /* There's a weird "PTHREAD" vs "THREAD" mismatch here. */
  {"PTHREAD_DESTRUCTOR_ITERATIONS", _SC_THREAD_DESTRUCTOR_ITERATIONS},
  {"PTHREAD_KEYS_MAX", _SC_THREAD_KEYS_MAX},
  {"PTHREAD_STACK_MIN", _SC_THREAD_STACK_MIN},
  {"PTHREAD_THREADS_MAX", _SC_THREAD_THREADS_MAX},
  CONF(RE_DUP_MAX), CONF(RTSIG_MAX), CONF(SEM_NSEMS_MAX), CONF(SEM_VALUE_MAX),
  CONF(SIGQUEUE_MAX), CONF(STREAM_MAX), CONF(SYMLOOP_MAX), CONF(TIMER_MAX),
  CONF(TTY_NAME_MAX), CONF(TZNAME_MAX), CONF(NPROCESSORS_CONF),
  CONF(NPROCESSORS_ONLN)
};

// Strings out of a header
struct config confstrs[] = {
#undef CONF
#define CONF(n) {#n,_CS_ ## n}
  CONF(PATH), CONF(V7_ENV)
};

// Integers out of a header
struct config limits[] = {
#undef CONF
#define CONF(n) {#n,n}
  CONF(_POSIX_AIO_LISTIO_MAX), CONF(_POSIX_AIO_MAX), CONF(_POSIX_ARG_MAX),
  CONF(_POSIX_CHILD_MAX), CONF(_POSIX_DELAYTIMER_MAX),
  CONF(_POSIX_HOST_NAME_MAX), CONF(_POSIX_LINK_MAX),
  CONF(_POSIX_LOGIN_NAME_MAX), CONF(_POSIX_MAX_CANON),
  CONF(_POSIX_MAX_INPUT), CONF(_POSIX_NAME_MAX), CONF(_POSIX_NGROUPS_MAX),
  CONF(_POSIX_OPEN_MAX), CONF(_POSIX_PATH_MAX), CONF(_POSIX_PIPE_BUF),
  CONF(_POSIX_RE_DUP_MAX), CONF(_POSIX_RTSIG_MAX), CONF(_POSIX_SEM_NSEMS_MAX),
  CONF(_POSIX_SEM_VALUE_MAX), CONF(_POSIX_SIGQUEUE_MAX), CONF(_POSIX_SSIZE_MAX),
  CONF(_POSIX_STREAM_MAX), CONF(_POSIX_SYMLINK_MAX), CONF(_POSIX_SYMLOOP_MAX),
  CONF(_POSIX_THREAD_DESTRUCTOR_ITERATIONS), CONF(_POSIX_THREAD_KEYS_MAX),
  CONF(_POSIX_THREAD_THREADS_MAX), CONF(_POSIX_TIMER_MAX),
  CONF(_POSIX_TTY_NAME_MAX), CONF(_POSIX_TZNAME_MAX),
  CONF(_POSIX2_BC_BASE_MAX), CONF(_POSIX2_BC_DIM_MAX),
  CONF(_POSIX2_BC_SCALE_MAX), CONF(_POSIX2_BC_STRING_MAX),
  CONF(_POSIX2_CHARCLASS_NAME_MAX), CONF(_POSIX2_COLL_WEIGHTS_MAX),
  CONF(_POSIX2_EXPR_NEST_MAX), CONF(_POSIX2_LINE_MAX), CONF(_POSIX2_RE_DUP_MAX)
};

// Names that default to blank
struct config others[] = {
  {"LFS_CFLAGS", 0}, {"LFS_LDFLAGS", 0}, {"LFS_LIBS", 0}
};

void getconf_main(void)
{
  struct config *configs[] = {sysconfs, confstrs, limits, others};
  char **args;
  int i, j, lens[] = {ARRAY_LEN(sysconfs), ARRAY_LEN(confstrs),
    ARRAY_LEN(limits), ARRAY_LEN(others)};

  if (toys.optflags&FLAG_l) {
    for (i = 0; i<4; i++) for (j = 0; j<lens[i]; j++) puts(configs[i][j].name);

    return;
  }

  for (args = toys.optargs; *args; args++) {
    char *name = *args;

    // Workaround for autogen using CS_PATH instead of PATH
    if (!strcmp("CS_PATH", name)) name += 3;

    for (i = 0; i<4; i++) for (j = 0; j<lens[i]; j++) {
      struct config *c = &configs[i][j];

      if (strcmp(c->name, name)) continue;

      if (!i) printf("%ld\n", sysconf(c->value));
      else if (i==1) {
        confstr(c->value, toybuf, sizeof(toybuf));
        puts(toybuf);
      } else if (i==2) printf("%d\n", c->value);
      // For legacy kernel build
      else if (sizeof(long)==4 && !j)
        puts("-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64");

      goto cont;
    }
    error_msg("bad '%s'", name);
cont:
    ;
  }
}
