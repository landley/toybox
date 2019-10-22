/* ln.c - Create filesystem links
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/ln.html

USE_LN(NEWTOY(ln, "<1t:Tvnfrs", TOYFLAG_BIN))

config LN
  bool "ln"
  default y
  help
    usage: ln [-sfnrv] [-t DIR] [FROM...] TO

    Create a link between FROM and TO.
    One/two/many arguments work like "mv" or "cp".

    -s	Create a symbolic link
    -f	Force the creation of the link, even if TO already exists
    -n	Symlink at TO treated as file
    -r	Create symbolic links relative to link location
    -t	Create links in DIR
    -T	TO always treated as file, max 2 arguments
    -v	Verbose
*/

#define FOR_ln
#include "toys.h"

GLOBALS(
  char *t;
)

void ln_main(void)
{
  char *dest = TT.t ? TT.t : toys.optargs[--toys.optc], *new;
  struct stat buf;
  int i;

  // With one argument, create link in current directory.
  if (!toys.optc) {
    toys.optc++;
    dest=".";
  }

  if (FLAG(T) && toys.optc>1) help_exit("Max 2 arguments");
  // Is destination a directory?
  if (!((FLAG(n)||FLAG(T)) ? lstat : stat)(dest, &buf)) {
    i = S_ISDIR(buf.st_mode);

    if ((FLAG(T) && i) || (!i && (toys.optc>1 || TT.t)))
      error_exit("'%s' %s a directory", dest, i ? "is" : "not");
  } else buf.st_mode = 0;

  for (i=0; i<toys.optc; i++) {
    int rc;
    char *oldnew, *try = toys.optargs[i];

    if (S_ISDIR(buf.st_mode)) new = xmprintf("%s/%s", dest, basename(try));
    else new = dest;

    if (FLAG(r)) {
      if (FLAG(s)) {
        int j = 0, k;
        // absolute paths needed for comparison
        char *s = getdirname(new), *ss, *try2 = xabspath(try, -1)
          *abs = xmprintf("%s/%s", ss = xabspath(s, -1), getbasename(new));

        free(s);
        free(ss);

        while (try2[j] == abs[j] && strchr(try2 + j, '/')) j++;
        for(k = j, rc = 0; abs[k]; k++) if(abs[k] == '/') rc++;
        free(abs);

        try = xzalloc(strlen(try2) - j + rc * 3 + 1);
        while (rc--) strcat(try, "../");
        strcat(try, try2 + j);
        free(try2);
      } else error_exit("-r only with -s");
    }

    // Force needs to unlink the existing target (if any). Do that by creating
    // a temp version and renaming it over the old one, so we can retain the
    // old file in cases we can't replace it (such as hardlink between mounts).
    oldnew = new;
    if (FLAG(f)) {
      new = xmprintf("%s_XXXXXX", new);
      rc = mkstemp(new);
      if (rc >= 0) {
        close(rc);
        if (unlink(new)) perror_msg("unlink '%s'", new);
      }
    }

    rc = FLAG(s) ? symlink(try, new) : link(try, new);
    if (FLAG(f)) {
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
    if (rc) perror_msg("cannot create %s link from '%s' to '%s'",
                       FLAG(s) ? "symbolic" : "hard", try, new);
    else if (FLAG(v)) fprintf(stderr, "'%s' -> '%s'\n", new, try);

    if (FLAG(r)) free(try);
    if (new != dest) free(new);
  }
}
