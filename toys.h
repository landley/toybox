/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "generated/config.h"

#include "lib/portability.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <libgen.h>
#include <math.h>
#include <pty.h>
#include <pwd.h>
#include <sched.h>
#include <setjmp.h>
#include <sched.h>
#include <shadow.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/swap.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <utmpx.h>

// Internationalization support

#include <locale.h>
#include <wchar.h>
#include <wctype.h>

#include "lib/lib.h"
#include "toys/e2fs.h"

// Get list of function prototypes for all enabled command_main() functions.

#define NEWTOY(name, opts, flags) void name##_main(void);
#define OLDTOY(name, oldname, opts, flags)
#include "generated/newtoys.h"
#include "generated/oldtoys.h"
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
  int exitval;             // Value error_exit feeds to exit()
  char **argv;             // Original command line arguments
  unsigned optflags;       // Command line option flags from get_optflags()
  char **optargs;          // Arguments left over from get_optflags()
  int optc;                // Count of optargs
  int exithelp;            // Should error_exit print a usage message first?
  int old_umask;           // Old umask preserved by TOYFLAG_UMASK
  jmp_buf *rebound;        // longjmp here instead of exit when do_rebound set
} toys;

// Two big temporary buffers: one for use by commands, one for library functions

extern char toybuf[4096], libbuf[4096];

extern char **environ;

#define GLOBALS(...)

#define ARRAY_LEN(array) (sizeof(array)/sizeof(*array))
