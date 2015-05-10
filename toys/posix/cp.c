/* Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/cp.html
 *
 * Posix says "cp -Rf dir file" shouldn't delete file, but our -f does.

// options shared between mv/cp must be in same order (right to left)
// for FLAG macros to work out right in shared infrastructure.

USE_CP(NEWTOY(cp, "<2RHLPp"USE_CP_MORE("rdaslvnF(remove-destination)")"fi[-HLP"USE_CP_MORE("d")"]"USE_CP_MORE("[-ni]"), TOYFLAG_BIN))
USE_MV(NEWTOY(mv, "<2"USE_CP_MORE("vnF")"fi"USE_CP_MORE("[-ni]"), TOYFLAG_BIN))
USE_INSTALL(NEWTOY(install, "<1cdDpsvm:o:g:", TOYFLAG_USR|TOYFLAG_BIN))

config CP
  bool "cp"
  default y
  help
    usage: cp [-fipRHLP] SOURCE... DEST

    Copy files from SOURCE to DEST.  If more than one SOURCE, DEST must
    be a directory.

    -f	delete destination files we can't write to
    -F	delete any existing destination file first (--remove-destination)
    -i	interactive, prompt before overwriting existing DEST
    -p	preserve timestamps, ownership, and permissions
    -R	recurse into subdirectories (DEST must be a directory)
    -H	Follow symlinks listed on command line
    -L	Follow all symlinks
    -P	Do not follow symlinks [default]

config CP_MORE
  bool "cp -adlnrsv options"
  default y
  depends on CP
  help
    usage: cp [-adlnrsv]

    -a	same as -dpr
    -d	don't dereference symlinks
    -l	hard link instead of copy
    -n	no clobber (don't overwrite DEST)
    -r	synonym for -R
    -s	symlink instead of copy
    -v	verbose

config MV
  bool "mv"
  default y
  depends on CP
  help
    usage: mv [-fi] SOURCE... DEST"

    -f	force copy by deleting destination file
    -i	interactive, prompt before overwriting existing DEST

config MV_MORE
  bool
  default y
  depends on MV && CP_MORE
  help
    usage: mv [-vn]

    -v	verbose
    -n	no clobber (don't overwrite DEST)

config INSTALL
  bool "install"
  default y
  depends on CP && CP_MORE
  help
    usage: install [-dDpsv] [-o USER] [-g GROUP] [-m MODE] [SOURCE...] DEST

    Copy files and set attributes.

    -d	Act like mkdir -p
    -D	Create leading directories for DEST
    -g	Make copy belong to GROUP
    -m	Set permissions to MODE
    -o	Make copy belong to USER
    -p	Preserve timestamps
    -s	Call "strip -p"
    -v	Verbose
*/

#define FOR_cp
#include "toys.h"

GLOBALS(
  // install's options
  char *group;
  char *user;
  char *mode;

  char *destname;
  struct stat top;
  int (*callback)(struct dirtree *try);
  uid_t uid;
  gid_t gid;
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
  if (S_ISDIR(try->st.st_mode) && try->again) {
    fdout = try->extra;
    err = 0;
  } else {

    // -d is only the same as -r for symlinks, not for directories
    if (S_ISLNK(try->st.st_mode) && (flags & FLAG_d)) flags |= FLAG_r;

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
      } else if ((flags & FLAG_F) && unlinkat(cfd, catch, 0)) {
        error_msg("unlink '%s'", catch);
        return 0;
      } else if (flags & FLAG_n) return 0;
      else if (flags & FLAG_i) {
        fprintf(stderr, "%s: overwrite '%s'", toys.which->name,
          s = dirtree_path(try, 0));
        free(s);
        if (!yesno("", 1)) return 0;
      }
    }

    if (flags & FLAG_v) {
      char *s = dirtree_path(try, 0);
      printf("%s '%s'\n", toys.which->name, s);
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
            if (!fstat(try->extra, &st2) && S_ISDIR(st2.st_mode))
              return DIRTREE_COMEAGAIN
                     | (DIRTREE_SYMFOLLOW*!!(toys.optflags&FLAG_L));

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
          char *s2 = xmprintf("%*c%s", 3*dotdots, ' ', s);
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
      int rc;

      // Inability to set these isn't fatal, some require root access.

      times[0] = try->st.st_atim;
      times[1] = try->st.st_mtim;

      // If we can't get a filehandle to the actual object, use racy functions
      if (fdout == AT_FDCWD)
        rc = fchownat(cfd, catch, try->st.st_uid, try->st.st_gid,
                      AT_SYMLINK_NOFOLLOW);
      else rc = fchown(fdout, try->st.st_uid, try->st.st_gid);
      if (rc) {
        char *pp;

        perror_msg("chown '%s'", pp = dirtree_path(try, 0));
        free(pp);
      }

      // permission bits already correct for mknod and don't apply to symlink
      if (fdout == AT_FDCWD) utimensat(cfd, catch, times, AT_SYMLINK_NOFOLLOW);
      else {
        futimens(fdout, times);
        fchmod(fdout, try->st.st_mode);
      }
    }

    if (fdout != AT_FDCWD) xclose(fdout);

    if (CFG_MV && toys.which->name[0] == 'm')
      if (unlinkat(tfd, try->name, S_ISDIR(try->st.st_mode) ? AT_REMOVEDIR :0))
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

  if (!TT.callback) TT.callback = cp_node;

  // Loop through sources

  for (i=0; i<toys.optc; i++) {
    struct dirtree *new;
    char *src = toys.optargs[i];
    int rc = 1;

    if (destdir) TT.destname = xmprintf("%s/%s", destname, basename(src));
    else TT.destname = destname;

    errno = EXDEV;
    if (CFG_MV && toys.which->name[0] == 'm') {
      if (!(toys.optflags & FLAG_f)) {
        struct stat st;

        // Technically "is writeable" is more complicated (022 is not writeable
        // by the owner, just everybody _else_) but I don't care.
        if (!stat(TT.destname, &st)
          && ((toys.optflags & FLAG_i) || !(st.st_mode & 0222)))
        {
          fprintf(stderr, "%s: overwrite '%s'", toys.which->name, TT.destname);
          if (!yesno("", 1)) rc = 0;
          else unlink(src);
        }
      }

      if (rc) rc = rename(src, TT.destname);
    }

    // Skip nonexistent sources
    if (rc) {
      if (errno!=EXDEV ||
        !(new = dirtree_start(src, toys.optflags&(FLAG_H|FLAG_L))))
          perror_msg("bad '%s'", src);
      else dirtree_handle_callback(new, TT.callback);
    }
    if (destdir) free(TT.destname);
  }
}

void mv_main(void)
{
  cp_main();
}

#define CLEANUP_cp
#define FOR_install
#include <generated/flags.h>

static int install_node(struct dirtree *try)
{
  if (TT.mode) try->st.st_mode = string_to_mode(TT.mode, try->st.st_mode);
  if (TT.group) try->st.st_gid = TT.gid;
  if (TT.user) try->st.st_uid = TT.uid;

  // Always returns 0 because no -r
  cp_node(try);

  // No -r so always one level deep, so destname as set by cp_node() is correct
  if (toys.optflags & FLAG_s)
    if (xrun((char *[]){"strip", "-p", TT.destname, 0})) toys.exitval = 1;

  return 0;
}

void install_main(void)
{
  char **ss;
  int flags = toys.optflags;

  if (flags & FLAG_d) {
    for (ss = toys.optargs; *ss; ss++) {
      if (mkpathat(AT_FDCWD, *ss, 0777, 3)) perror_msg("%s", *ss);
      if (flags & FLAG_v) printf("%s\n", *ss);
    }

    return;
  }

  if (toys.optflags & FLAG_D) {
    TT.destname = toys.optargs[toys.optc-1];
    if (mkpathat(AT_FDCWD, TT.destname, 0, 2))
      perror_exit("-D '%s'", TT.destname);
    if (toys.optc == 1) return;
  }
  if (toys.optc < 2) error_exit("needs 2 args");

  // Translate flags from install to cp
  toys.optflags = 4;  // Force cp's FLAG_F
  if (flags & FLAG_v) toys.optflags |= 8; // cp's FLAG_v
  if (flags & (FLAG_p|FLAG_o|FLAG_g)) toys.optflags |= 512; // cp's FLAG_p

  if (TT.user) TT.uid = xgetpwnamid(TT.user)->pw_uid;
  if (TT.group) TT.gid = xgetgrnamid(TT.group)->gr_gid;

  TT.callback = install_node;
  cp_main();
}
