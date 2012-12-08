/* rm.c - remove files
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/rm.html

USE_RM(NEWTOY(rm, "<1fiRr[-fi]", TOYFLAG_BIN))

config RM
  bool "rm"
  default y
  help
    usage: rm [-fiRr] FILE...

    Remove each argument from the filesystem.

    -f	force: remove without confirmation, no error if it doesn't exist
    -i	interactive: prompt for confirmation
    -rR	recursive: remove directory contents
*/

#define FOR_rm
#include "toys.h"

static int do_rm(struct dirtree *try)
{
  int fd = dirtree_parentfd(try), flags = toys.optflags;
  int dir = S_ISDIR(try->st.st_mode), or = 0, using = 0;

  // Skip . and .. (yes, even explicitly on the command line: posix says to)
  if (!dirtree_notdotdot(try)) return 0;

  // Intentionally fail non-recursive attempts to remove even an empty dir
  // (via wrong flags to unlinkat) because POSIX says to.
  if (dir && !(flags & (FLAG_r|FLAG_R))) goto skip;

  // This is either the posix section 2(b) prompt or the section 3 prompt.
  if (!(flags & FLAG_f) && faccessat(fd, try->name, W_OK, AT_SYMLINK_NOFOLLOW))
    or++;
  if (!(dir && try->data == -1) && ((or && isatty(0)) || (flags & FLAG_i))) {
    char *s = dirtree_path(try, 0);
    fprintf(stderr, "rm %s%s", or ? "ro " : "", dir ? "dir " : "");
    or = yesno(s, 0);
    free(s);
    if (!or) goto nodelete;
  }

  // handle directory recursion
  if (dir) {

    if (try->data != -1) return DIRTREE_COMEAGAIN;
    using = AT_REMOVEDIR;
    if (try->symlink) goto nodelete;
    if (flags & FLAG_i) {
      char *s = dirtree_path(try, 0);
      // This is the section 2(d) prompt. (Yes, posix says to prompt twice.)
      fprintf(stderr, "rmdir ");
      or = yesno(s, 0);
      free(s);
      if (!or) goto nodelete;
    }
  }

skip:
  if (unlinkat(fd, try->name, using)) {
    perror_msg("%s", try->name);
    toys.exitval = 1;
nodelete:
    if (try->parent) try->parent->symlink = (char *)1;
  }

  return 0;
}

void rm_main(void)
{
  char **s;

  for (s = toys.optargs; *s; s++) {
    if (!strcmp(*s, "/")) {
      error_msg("rm /. if you mean it");
      toys.exitval = 1;
      continue;
    }

    // There's a race here where a file removed between this access and
    // dirtree's stat would report the nonexistence as an error, but that's
    // not a normal "it didn't exist" so I'm ok with it.
    if ((toys.optflags & FLAG_f) && (access(*s, F_OK) && errno == ENOENT))
      continue;
    dirtree_read(*s, do_rm);
  }
}
