/* ln.c - Create filesystem links
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/ln.html

USE_LN(NEWTOY(ln, "<1vnfs", TOYFLAG_BIN))

config LN
  bool "ln"
  default y
  help
    usage: ln [-sfnv] [FROM...] TO

    Create a link between FROM and TO.
    With only one argument, create link in current directory.

    -s	Create a symbolic link
    -f	Force the creation of the link, even if TO already exists
    -n	Symlink at destination treated as file
    -v	Verbose
*/

#define FOR_ln
#include "toys.h"

void ln_main(void)
{
  char *dest = toys.optargs[--toys.optc], *new;
  struct stat buf;
  int i;

  // With one argument, create link in current directory.
  if (!toys.optc) {
    toys.optc++;
    dest=".";
  }

  // Is destination a directory?
  if (((toys.optflags&FLAG_n) ? lstat : stat)(dest, &buf)
    || !S_ISDIR(buf.st_mode))
  {
    if (toys.optc>1) error_exit("'%s' not a directory", dest);
    buf.st_mode = 0;
  }

  for (i=0; i<toys.optc; i++) {
    int rc;
    char *oldnew, *try = toys.optargs[i];

    if (S_ISDIR(buf.st_mode)) new = xmprintf("%s/%s", dest, basename(try));
    else new = dest;

    // Force needs to unlink the existing target (if any). Do that by creating
    // a temp version and renaming it over the old one, so we can retain the
    // old file in cases we can't replace it (such as hardlink between mounts).
    oldnew = new;
    if (toys.optflags & FLAG_f) {
      new = xmprintf("%s_XXXXXX", new);
      rc = mkstemp(new);
      if (rc >= 0) {
        close(rc);
        if (unlink(new)) perror_msg("unlink '%s'", new);
      }
    }

    rc = (toys.optflags & FLAG_s) ? symlink(try, new) : link(try, new);
    if (toys.optflags & FLAG_f) {
      if (!rc) {
        int temp;

        rc = rename(new, oldnew);
        temp = errno;
        if (rc && unlink(new)) perror_msg("unlink '%s'", new);
        errno = temp;
      }
      free(new);
      new = oldnew;
    }
    if (rc)
      perror_msg("cannot create %s link from '%s' to '%s'",
        (toys.optflags & FLAG_s) ? "symbolic" : "hard", try, new);
    else
      if (toys.optflags & FLAG_v) fprintf(stderr, "'%s' -> '%s'\n", new, try);

    if (new != dest) free(new);
  }
}
