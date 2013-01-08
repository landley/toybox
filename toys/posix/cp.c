/* Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/cp.html
 *
 * TODO: sHLP

USE_CP(NEWTOY(cp, "<2"USE_CP_MORE("rdavsl")"RHLPfip", TOYFLAG_BIN))

config CP
  bool "cp"
  default y
  help
    usage: cp [-fipRHLP] SOURCE... DEST

    Copy files from SOURCE to DEST.  If more than one SOURCE, DEST must
    be a directory.

    -f	force copy by deleting destination file
    -i	interactive, prompt before overwriting existing DEST
    -p	preserve timestamps, ownership, and permissions
    -R	recurse into subdirectories (DEST must be a directory)
    -H	Follow symlinks listed on command line
    -L	Follow all symlinks
    -P	Do not follow symlinks [default]

config CP_MORE
  bool "cp -rdavsl options"
  default y
  depends on CP
  help
    usage: cp [-rdavsl]

    -r	synonym for -R
    -d	don't dereference symlinks
    -a	same as -dpr
    -l	hard link instead of copy
    -s	symlink instead of copy
    -v	verbose
*/

#define FOR_cp
#include "toys.h"

// TODO: PLHlsd

GLOBALS(
  char *destname;
  struct stat top;
)

// Callback from dirtree_read() for each file/directory under a source dir.

int cp_node(struct dirtree *try)
{
  int fdout, cfd = try->parent ? try->parent->extra : AT_FDCWD,
      tfd = dirtree_parentfd(try);
  char *catch = try->parent ? try->name : TT.destname, *err = "%s";
  struct stat cst;

  if (!dirtree_notdotdot(try)) return 0;

  // If returning from COMEAGAIN, jump straight to -p logic at end.
  if (S_ISDIR(try->st.st_mode) && try->data == -1) {
    fdout = try->extra;
    err = 0;
    goto dashp;
  }

  // Detect recursive copies via repeated top node (cp -R .. .) or
  // identical source/target (fun with hardlinks).
  if ((TT.top.st_dev == try->st.st_dev && TT.top.st_ino == try->st.st_ino
       && (catch = TT.destname))
      || (!fstatat(cfd, catch, &cst, 0) && cst.st_dev == try->st.st_dev
       && cst.st_ino == try->st.st_ino))
  {
    char *s = dirtree_path(try, 0);
    error_msg("'%s' is '%s'", catch, s);
    free(s);

    return 0;
  }

  // Handle -i and -v

  if ((toys.optflags & FLAG_i) && !faccessat(cfd, catch, R_OK, 0)
    && !yesno("cp: overwrite", 1)) return 0;

  if (toys.optflags & FLAG_v) {
    char *s = dirtree_path(try, 0);
    printf("cp '%s'\n", s);
    free(s);
  }

  // Copy directory or file to destination.

  if (S_ISDIR(try->st.st_mode)) {
    struct stat st2;

    if (!(toys.optflags & (FLAG_a|FLAG_r|FLAG_R))) {
      err = "Skipped dir '%s'";
      catch = try->name;

    // Always make directory writeable to us, so we can create files in it.
    //
    // Yes, there's a race window between mkdir() and open() so it's
    // possible that -p can be made to chown a directory other than the one
    // we created. The closest we can do to closing this is make sure
    // that what we open _is_ a directory rather than something else.

    } else if ((mkdirat(cfd, catch, try->st.st_mode | 0200) && errno != EEXIST)
      || 0>(try->extra = openat(cfd, catch, 0)) || fstat(try->extra, &st2)
      || !S_ISDIR(st2.st_mode));
    else return DIRTREE_COMEAGAIN;
  } else if (S_ISLNK(try->st.st_mode)
    && (try->parent || (toys.optflags & (FLAG_a|FLAG_d))))
  {
    int i = readlinkat(tfd, try->name, toybuf, sizeof(toybuf));
    if (i > 0 && i < sizeof(toybuf) && !symlinkat(toybuf, cfd, catch)) err = 0;
  } else if (toys.optflags & FLAG_l) {
    if (!linkat(tfd, try->name, cfd, catch, 0)) err = 0;
  } else {
    int fdin, i;

    fdin = openat(tfd, try->name, O_RDONLY);
    if (fdin < 0) catch = try->name;
    else {
      for (i=2 ; i; i--) {
        fdout = openat(cfd, catch, O_RDWR|O_CREAT|O_TRUNC, try->st.st_mode);
        if (fdout>=0 || !(toys.optflags & FLAG_f)) break;
        unlinkat(cfd, catch, 0);
      }
      if (fdout >= 0) {
        xsendfile(fdin, fdout);
        err = 0;
      }
      close(fdin);
    }

dashp:
    if (toys.optflags & (FLAG_a|FLAG_p)) {
      struct timespec times[2];

      // Inability to set these isn't fatal, some require root access.

      fchown(fdout, try->st.st_uid, try->st.st_gid);
      times[0] = try->st.st_atim;
      times[1] = try->st.st_mtim;
      futimens(fdout, times);
      fchmod(fdout, try->st.st_mode);
    }

    xclose(fdout);
  }

  if (err) perror_msg(err, catch);
  return 0;
}

void cp_main(void)
{
  char *destname = toys.optargs[--toys.optc];
  int i, destdir = !stat(destname, &TT.top) && S_ISDIR(TT.top.st_mode);

  if (toys.optc>1 && !destdir) error_exit("'%s' not directory", destname);

  // Loop through sources

  for (i=0; i<toys.optc; i++) {
    struct dirtree *new;
    char *src = toys.optargs[i];

    // Skip nonexistent sources
    if (!(new = dirtree_add_node(0, src, !(toys.optflags & (FLAG_d|FLAG_a)))))
      perror_msg("bad '%s'", src);
    else {
      if (destdir) TT.destname = xmsprintf("%s/%s", destname, basename(src));
      else TT.destname = destname;
      dirtree_handle_callback(new, cp_node);
      if (destdir) free(TT.destname);
    }
  }
}
