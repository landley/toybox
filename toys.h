/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

// Stuff that needs to go before the standard headers

#include "generated/config.h"
#include "lib/portability.h"

// General posix-2008 headers
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <paths.h>
#include <pwd.h>
#include <regex.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

// Posix networking

#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

// Internationalization support (also in POSIX and LSB)

#include <langinfo.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>

// LSB 4.1 headers
#include <sys/ioctl.h>

#include "lib/lib.h"
#include "lib/lsm.h"
#include "lib/toyflags.h"

// Get list of function prototypes for all enabled command_main() functions.

#define NEWTOY(name, opts, flags) void name##_main(void);
#define OLDTOY(name, oldname, flags) void oldname##_main(void);
#include "generated/newtoys.h"
#include "generated/flags.h"
#include "generated/globals.h"
#include "generated/tags.h"

// These live in main.c

struct toy_list *toy_find(char *name);
void toy_init(struct toy_list *which, char *argv[]);
void toy_singleinit(struct toy_list *which, char *argv[]);
void toy_exec(char *argv[]);

// Array of available commands

extern struct toy_list {
  char *name;
  void (*toy_main)(void);
  char *options;
  unsigned flags;
} toy_list[];

// Global context shared by all commands.

extern struct toy_context {
  struct toy_list *which;  // Which entry in toy_list is this one?
  char **argv;             // Original command line arguments
  char **optargs;          // Arguments left over from get_optflags()
  unsigned long long optflags; // Command line option flags from get_optflags()
  int optc;                // Count of optargs
  short toycount;          // Total number of commands in this build
  char exitval;            // Value error_exit feeds to exit()
  char wasroot;            // dropped setuid

  // toy_init() should not zero past here.
  sigjmp_buf *rebound;     // siglongjmp here instead of exit when do_rebound
  struct arg_list *xexit;  // atexit() functions for xexit(), set by sigatexit()
  void *stacktop;          // nested toy_exec() call count, or 0 if vforked
  int envc;                // Count of original environ entries
  int old_umask;           // Old umask preserved by TOYFLAG_UMASK
  short signal;            // generic_signal() records what signal it saw here
  int signalfd;            // and writes signal to this fd, if set
} toys;

// Two big temporary buffers: one for use by commands, one for library functions

extern char *toybox_version, toybuf[4096], libbuf[4096];

extern char **environ;

#define FLAG(x) (toys.optflags&FLAG_##x)

#define GLOBALS(...)
#define ARRAY_LEN(array) (sizeof(array)/sizeof(*array))
#define TAGGED_ARRAY(X, ...) {__VA_ARGS__}

#ifndef TOYBOX_VERSION
#ifndef TOYBOX_VENDOR
#define TOYBOX_VENDOR ""
#endif
#define TOYBOX_VERSION "0.8.4"TOYBOX_VENDOR
#endif
