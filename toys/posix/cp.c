/* Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/cp.html
 *
 * TODO: sHLP

// This is subtle: MV options must be in same order (right to left) as CP
// for FLAG_X macros to work out right.

USE_CP(NEWTOY(cp, "<2RHLPp"USE_CP_MORE("rdaslvn")"fi"USE_CP_MORE("[-ni]"), TOYFLAG_BIN))
USE_CP_MV(OLDTOY(mv, cp, "<2"USE_CP_MORE("vn")"fi"USE_CP_MORE("[-ni]"), TOYFLAG_BIN))

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

    -a	same as -dpr
    -d	don't dereference symlinks
    -l	hard link instead of copy
    -n	no clobber (don't overwrite DEST)
    -r	synonym for -R
    -s	symlink instead of copy
    -v	verbose

config CP_MV
  bool "mv"
  default y
  depends on CP
  help
    usage: mv [-fi] SOURCE... DEST"

    -f	force copy by deleting destination file
    -i	interactive, prompt before overwriting existing DEST

config CP_MV_MORE
  default y
  depends on CP_MV && CP_MORE
  help
    usage: mv [-vn]

    -v	verbose
    -n	no clobber (don't overwrite DEST)
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
  int fdout = -1, cfd = try->parent ? try->parent->extra : AT_FDCWD,
      tfd = dirtree_parentfd(try);
  unsigned flags = toys.optflags;
  char *catch = try->parent ? try->name : TT.destname, *err = "%s";
  struct stat cst;

  if (!dirtree_notdotdot(try)) return 0;

  // If returning from COMEAGAIN, jump straight to -p logic at end.
  if (S_ISDIR(try->st.st_mode) && try->data == -1) {
    fdout = try->extra;
    err = 0;
  } else {

    // -d is only the same as -r for symlinks, not for directories
    if (S_ISLNK(try->st.st_mode) & (flags & FLAG_d)) flags |= FLAG_r;

    // Detect recursive copies via repeated top node (cp -R .. .) or
    // identical source/target (fun with hardlinks).
    if ((TT.top.st_dev == try->st.st_dev && TT.top.st_ino == try->st.st_ino
         && (catch = TT.destname))
        || (!fstatat(cfd, catch, &cst, 0) && cst.st_dev == try->st.st_dev
         && cst.st_ino == try->st.st_ino))
    {
      error_msg("'%s' is '%s'", catch, err = dirtree_path(try, 0));
      free(err);

      return 0;
    }

    // Handle -inv

    if (!faccessat(cfd, catch, F_OK, 0) && !S_ISDIR(cst.st_mode)) {
      char *s;

      if (S_ISDIR(try->st.st_dev)) {
        error_msg("dir at '%s'", s = dirtree_path(try, 0));
        free(s);
        return 0;
      } else if (flags & FLAG_n) return 0;
      else if (flags & FLAG_i) {
        fprintf(stderr, "cp: overwrite '%s'", s = dirtree_path(try, 0));
        free(s);
        if (!yesno("", 1)) return 0;
      }
    }

    if (flags & FLAG_v) {
      char *s = dirtree_path(try, 0);
      printf("cp '%s'\n", s);
      free(s);
    }

    // Loop for -f retry after unlink
    do {

      // directory, hardlink, symlink, mknod (char, block, fifo, socket), file

      // Copy directory

      if (S_ISDIR(try->st.st_mode)) {
        struct stat st2;

        if (!(flags & (FLAG_a|FLAG_r|FLAG_R))) {
          err = "Skipped dir '%s'";
          catch = try->name;
          break;
        }

        // Always make directory writeable to us, so we can create files in it.
        //
        // Yes, there's a race window between mkdir() and open() so it's
        // possible that -p can be made to chown a directory other than the one
        // we created. The closest we can do to closing this is make sure
        // that what we open _is_ a directory rather than something else.

        if (!mkdirat(cfd, catch, try->st.st_mode | 0200) || errno == EEXIST)
          if (-1 != (try->extra = openat(cfd, catch, O_NOFOLLOW)))
            if (!fstat(try->extra, &st2))
              if (S_ISDIR(st2.st_mode)) return DIRTREE_COMEAGAIN;

      // Hardlink

      } else if (flags & FLAG_l) {
        if (!linkat(tfd, try->name, cfd, catch, 0)) err = 0;

      // Copy tree as symlinks. For non-absolute paths this involves
      // appending the right number of .. entries as you go down the tree.

      } else if (flags & FLAG_s) {
        char *s;
        struct dirtree *or;
        int dotdots = 0;

        s = dirtree_path(try, 0);
        for (or = try; or->parent; or = or->parent) dotdots++;

        if (*or->name == '/') dotdots = 0;
        if (dotdots) {
          char *s2 = xmsprintf("% *c%s", 3*dotdots, ' ', s);
          free(s);
          s = s2;
          while(dotdots--) {
            memcpy(s2, "../", 3);
            s2 += 3;
          }
        }
        if (!symlinkat(s, cfd, catch)) {
          err = 0;
          fdout = AT_FDCWD;
        }
        free(s);

      // Do something _other_ than copy contents of a file?
      } else if (!S_ISREG(try->st.st_mode)
                 && (try->parent || (flags & (FLAG_a|FLAG_r))))
      {
        int i;

        // make symlink, or make block/char/fifo/socket
        if (S_ISLNK(try->st.st_mode)
            ? (0 < (i = readlinkat(tfd, try->name, toybuf, sizeof(toybuf))) &&
               sizeof(toybuf) > i && !symlinkat(toybuf, cfd, catch))
            : !mknodat(cfd, catch, try->st.st_mode, try->st.st_rdev))
        {
          err = 0;
          fdout = AT_FDCWD;
        }

      // Copy contents of file.
      } else {
        int fdin;

        fdin = openat(tfd, try->name, O_RDONLY);
        if (fdin < 0) {
          catch = try->name;
          break;
        } else {
          fdout = openat(cfd, catch, O_RDWR|O_CREAT|O_TRUNC, try->st.st_mode);
          if (fdout >= 0) {
            xsendfile(fdin, fdout);
            err = 0;
          }
          close(fdin);
        }
      }
    } while (err && (flags & (FLAG_f|FLAG_n)) && !unlinkat(cfd, catch, 0));
  }

  if (fdout != -1) {
    if (flags & (FLAG_a|FLAG_p)) {
      struct timespec times[2];

      // Inability to set these isn't fatal, some require root access.

      times[0] = try->st.st_atim;
      times[1] = try->st.st_mtim;

      // If we can't get a filehandle to the actual object, use racy functions
      if (fdout == AT_FDCWD) {
        fchownat(cfd, catch, try->st.st_uid, try->st.st_gid,
                 AT_SYMLINK_NOFOLLOW);
        utimensat(cfd, catch, times, AT_SYMLINK_NOFOLLOW);
        // permission bits already correct for mknod, don't apply to symlink
      } else {
        fchown(fdout, try->st.st_uid, try->st.st_gid);
        futimens(fdout, times);
        fchmod(fdout, try->st.st_mode);
      }
    }

    if (fdout != AT_FDCWD) xclose(fdout);

    if (toys.which->name[0] == 'm')
      if (unlinkat(tfd, try->name, S_ISDIR(try->st.st_mode) ? AT_REMOVEDIR : 0))
        err = "%s";
  }

  if (err) perror_msg(err, catch);
  return 0;
}

void cp_main(void)
{
  char *destname = toys.optargs[--toys.optc];
  int i, destdir = !stat(destname, &TT.top) && S_ISDIR(TT.top.st_mode);

  if (toys.optc>1 && !destdir) error_exit("'%s' not directory", destname);
  if (toys.which->name[0] == 'm') toys.optflags |= FLAG_d|FLAG_p|FLAG_R;
  if (toys.optflags & (FLAG_a|FLAG_p)) umask(0);

  // Loop through sources

  for (i=0; i<toys.optc; i++) {
    struct dirtree *new;
    char *src = toys.optargs[i];
    int rc = 1;

    if (destdir) TT.destname = xmsprintf("%s/%s", destname, basename(src));
    else TT.destname = destname;

    errno = EXDEV;
    if (toys.which->name[0] == 'm') rc = rename(src, TT.destname);

    // Skip nonexistent sources
    if (rc) {
      if (errno != EXDEV ||
        !(new = dirtree_add_node(0, src, !(toys.optflags & (FLAG_d|FLAG_a)))))
          perror_msg("bad '%s'", src);
      else dirtree_handle_callback(new, cp_node);
    }
    if (destdir) free(TT.destname);
  }
}
