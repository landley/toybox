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

#include <locale.h>
#include <wchar.h>
#include <wctype.h>

// LSB 4.1 headers
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>

#include "lib/lib.h"
#include "toys/e2fs.h"

// Get list of function prototypes for all enabled command_main() functions.

#define NEWTOY(name, opts, flags) void name##_main(void);
#define OLDTOY(name, oldname, flags) void oldname##_main(void);
#include "generated/newtoys.h"
#include "generated/flags.h"
#include "generated/globals.h"

// These live in main.c

struct toy_list *toy_find(char *name);
void toy_init(struct toy_list *which, char *argv[]);
void toy_exec(char *argv[]);

// Flags describing command behavior.

#define TOYFLAG_USR      (1<<0)
#define TOYFLAG_BIN      (1<<1)
#define TOYFLAG_SBIN     (1<<2)
#define TOYMASK_LOCATION ((1<<4)-1)

// This is a shell built-in function, running in the same process context.
#define TOYFLAG_NOFORK   (1<<4)

// Start command with a umask of 0 (saves old umask in this.old_umask)
#define TOYFLAG_UMASK    (1<<5)

// This command runs as root.
#define TOYFLAG_STAYROOT (1<<6)
#define TOYFLAG_NEEDROOT (1<<7)
#define TOYFLAG_ROOTONLY (TOYFLAG_STAYROOT|TOYFLAG_NEEDROOT)

// Call setlocale to listen to environment variables.
// This invalidates sprintf("%.*s", size, string) as a valid length constraint.
#define TOYFLAG_LOCALE   (1<<8)

// Array of available commands

extern struct toy_list {
  char *name;
  void (*toy_main)(void);
  char *options;
  int flags;
} toy_list[];

// Global context shared by all commands.

extern struct toy_context {
  struct toy_list *which;  // Which entry in toy_list is this one?
  char **argv;             // Original command line arguments
  char **optargs;          // Arguments left over from get_optflags()
  unsigned optflags;       // Command line option flags from get_optflags()
  int exitval;             // Value error_exit feeds to exit()
  int optc;                // Count of optargs
  int exithelp;            // Should error_exit print a usage message first?
  int old_umask;           // Old umask preserved by TOYFLAG_UMASK
  int toycount;            // Total number of commands in this build
  int signal;              // generic_signal() records what signal it saw here
  int signalfd;            // and writes signal to this fd, if set

  // This is at the end so toy_init() doesn't zero it.
  jmp_buf *rebound;        // longjmp here instead of exit when do_rebound set
  int recursion;           // How many nested calls to toy_exec()
} toys;

// Two big temporary buffers: one for use by commands, one for library functions

extern char toybuf[4096], libbuf[4096];

extern char **environ;

#define GLOBALS(...)

#define ARRAY_LEN(array) (sizeof(array)/sizeof(*array))
