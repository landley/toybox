/* mkdir.c - Make directories
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/mkdir.html

USE_MKDIR(NEWTOY(mkdir, "<1vpm:", TOYFLAG_BIN|TOYFLAG_UMASK))

config MKDIR
  bool "mkdir"
  default y
  help
    usage: mkdir [-vp] [-m mode] [dirname...]
    Create one or more directories.

    -m	set permissions of directory to mode.
    -p	make parent directories as needed.
    -v	verbose
*/

#define FOR_mkdir
#include "toys.h"

GLOBALS(
  char *arg_mode;
)

// flags: 1=make last dir (with mode lastmode, otherwise skips last component)
//        2=make path (already exists is ok)
//        4=verbose
// returns 0 = path ok, 1 = error
int mkpathat(int atfd, char *dir, mode_t lastmode, int flags)
{
  struct stat buf;
  char *s;

  // mkdir -p one/two/three is not an error if the path already exists,
  // but is if "three" is a file. The others we dereference and catch
  // not-a-directory along the way, but the last one we must explicitly
  // test for. Might as well do it up front.

  if (!fstatat(atfd, dir, &buf, 0) && !S_ISDIR(buf.st_mode)) {
    errno = EEXIST;
    return 1;
  }

  // Skip leading / of absolute paths
  while (*dir == '/') dir++;

  for (s=dir; ;s++) {
    char save = 0;
    mode_t mode = (0777&~toys.old_umask)|0300;

    // Skip leading / of absolute paths.
    if (*s == '/' && (flags&2)) {
      save = *s;
      *s = 0;
    } else if (*s) continue;

    // Use the mode from the -m option only for the last directory.
    if (!save) {
      if (flags&1) mode = lastmode;
      else break;
    }

    if (mkdirat(atfd, dir, mode)) {
      if (!(flags&2) || errno != EEXIST) return 1;
    } else if (flags&4)
      fprintf(stderr, "%s: created directory '%s'\n", toys.which->name, dir);
    
    if (!(*s = save)) break;
  }

  return 0;
}

void mkdir_main(void)
{
  char **s;
  mode_t mode = (0777&~toys.old_umask);


  if (TT.arg_mode) mode = string_to_mode(TT.arg_mode, 0777);

  // Note, -p and -v flags line up with mkpathat() flags

  for (s=toys.optargs; *s; s++)
    if (mkpathat(AT_FDCWD, *s, mode, toys.optflags|1))
      perror_msg("'%s'", *s);
}
