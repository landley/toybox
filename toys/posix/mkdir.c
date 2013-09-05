/* mkdir.c - Make directories
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/mkdir.html

USE_MKDIR(NEWTOY(mkdir, "<1pm:", TOYFLAG_BIN|TOYFLAG_UMASK))

config MKDIR
  bool "mkdir"
  default y
  help
    usage: mkdir [-p] [-m mode] [dirname...]
    Create one or more directories.

    -p	make parent directories as needed.
    -m  set permissions of directory to mode.
*/

#define FOR_mkdir
#include "toys.h"

GLOBALS(
  char *arg_mode;

  mode_t mode;
)

static int do_mkdir(char *dir)
{
  struct stat buf;
  char *s;

  // mkdir -p one/two/three is not an error if the path already exists,
  // but is if "three" is a file.  The others we dereference and catch
  // not-a-directory along the way, but the last one we must explicitly
  // test for. Might as well do it up front.

  if (!stat(dir, &buf) && !S_ISDIR(buf.st_mode)) {
    errno = EEXIST;
    return 1;
  }

  for (s=dir; ; s++) {
    char save=0;
    mode_t mode = 0777&~toys.old_umask;

    // Skip leading / of absolute paths.
    if (s!=dir && *s == '/' && (toys.optflags&FLAG_p)) {
      save = *s;
      *s = 0;
    } else if (*s) continue;

    // Use the mode from the -m option only for the last directory.
    if (save == '/') mode |= 0300;
    else if (toys.optflags&FLAG_m) mode = TT.mode;

    if (mkdir(dir, mode)<0 && (!(toys.optflags&FLAG_p) || errno != EEXIST))
      return 1;

    if (!(*s = save)) break;
  }

  return 0;
}

void mkdir_main(void)
{
  char **s;

  if(toys.optflags&FLAG_m) TT.mode = string_to_mode(TT.arg_mode, 0777);

  for (s=toys.optargs; *s; s++) if (do_mkdir(*s)) perror_msg("'%s'", *s);
}
