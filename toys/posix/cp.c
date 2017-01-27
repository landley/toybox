/* Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/cp.html
 * And http://opengroup.org/onlinepubs/9699919799/utilities/mv.html
 * And http://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic.html#INSTALL
 *
 * Posix says "cp -Rf dir file" shouldn't delete file, but our -f does.
 *
 * Deviations from posix: -adlnrsvF, --preserve... about half the
 * functionality in this cp isn't in posix. Posix is stuck in the 1970's.
 *
 * TODO: --preserve=links
 * TODO: what's this _CP_mode system.posix_acl_ business? We chmod()?

// options shared between mv/cp must be in same order (right to left)
// for FLAG macros to work out right in shared infrastructure.

USE_CP(NEWTOY(cp, "<2"USE_CP_PRESERVE("(preserve):;")"RHLPprdaslvnF(remove-destination)fi[-HLPd][-ni]", TOYFLAG_BIN))
USE_MV(NEWTOY(mv, "<2vnF(remove-destination)fi[-ni]", TOYFLAG_BIN))
USE_INSTALL(NEWTOY(install, "<1cdDpsvm:o:g:", TOYFLAG_USR|TOYFLAG_BIN))

config CP
  bool "cp"
  default y
  help
    usage: cp [-adlnrsvfipRHLP] SOURCE... DEST

    Copy files from SOURCE to DEST.  If more than one SOURCE, DEST must
    be a directory.

    -f	delete destination files we can't write to
    -F	delete any existing destination file first (--remove-destination)
    -i	interactive, prompt before overwriting existing DEST
    -p	preserve timestamps, ownership, and mode
    -R	recurse into subdirectories (DEST must be a directory)
    -H	Follow symlinks listed on command line
    -L	Follow all symlinks
    -P	Do not follow symlinks [default]
    -a	same as -dpr
    -d	don't dereference symlinks
    -l	hard link instead of copy
    -n	no clobber (don't overwrite DEST)
    -r	synonym for -R
    -s	symlink instead of copy
    -v	verbose

config CP_PRESERVE
  bool "cp --preserve support"
  default y
  depends on CP
  help
    usage: cp [--preserve=motcxa]

    --preserve takes either a comma separated list of attributes, or the first
    letter(s) of:

            mode - permissions (ignore umask for rwx, copy suid and sticky bit)
       ownership - user and group
      timestamps - file creation, modification, and access times.
         context - security context
           xattr - extended attributes
             all - all of the above

config MV
  bool "mv"
  default y
  help
    usage: mv [-fivn] SOURCE... DEST"

    -f	force copy by deleting destination file
    -i	interactive, prompt before overwriting existing DEST
    -v	verbose
    -n	no clobber (don't overwrite DEST)

config INSTALL
  bool "install"
  default y
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

#define FORCE_FLAGS
#define FOR_cp
#include "toys.h"

GLOBALS(
  union {
    struct {
      // install's options
      char *group;
      char *user;
      char *mode;
    } i;
    struct {
      char *preserve;
    } c;
  };

  char *destname;
  struct stat top;
  int (*callback)(struct dirtree *try);
  uid_t uid;
  gid_t gid;
  int pflags;
)

struct cp_preserve {
  char *name;
} static const cp_preserve[] = TAGGED_ARRAY(CP,
  {"mode"}, {"ownership"}, {"timestamps"}, {"context"}, {"xattr"},
);

// Callback from dirtree_read() for each file/directory under a source dir.

static int cp_node(struct dirtree *try)
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

    // Handle -invF

    if (!faccessat(cfd, catch, F_OK, 0) && !S_ISDIR(cst.st_mode)) {
      char *s;

      if (S_ISDIR(try->st.st_mode)) {
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
        if (!yesno(1)) return 0;
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
            ? ((i = readlinkat0(tfd, try->name, toybuf, sizeof(toybuf))) &&
               !symlinkat(toybuf, cfd, catch))
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
        }
        fdout = openat(cfd, catch, O_RDWR|O_CREAT|O_TRUNC, try->st.st_mode);
        if (fdout >= 0) {
          xsendfile(fdin, fdout);
          err = 0;
        }

        // We only copy xattrs for files because there's no flistxattrat()
        if (TT.pflags&(_CP_xattr|_CP_context)) {
          ssize_t listlen = flistxattr(fdin, 0, 0), len;
          char *name, *value, *list;

          if (listlen>0) {
            list = xmalloc(listlen);
            flistxattr(fdin, list, listlen);
            list[listlen-1] = 0; // I do not trust this API.
            for (name = list; name-list < listlen; name += strlen(name)+1) {
              if (!(TT.pflags&_CP_xattr) && strncmp(name, "security.", 9))
                continue;
              if ((len = fgetxattr(fdin, name, 0, 0))>0) {
                value = xmalloc(len);
                if (len == fgetxattr(fdin, name, value, len))
                  if (fsetxattr(fdout, name, value, len, 0))
                    perror_msg("%s setxattr(%s=%s)", catch, name, value);
                free(value);
              }
            }
            free(list);
          }
        }

        close(fdin);
      }
    } while (err && (flags & (FLAG_f|FLAG_n)) && !unlinkat(cfd, catch, 0));
  }

  // Did we make a thing?
  if (fdout != -1) {
    int rc;

    // Inability to set --preserve isn't fatal, some require root access.

    // ownership
    if (TT.pflags & _CP_ownership) {

      // permission bits already correct for mknod and don't apply to symlink
      // If we can't get a filehandle to the actual object, use racy functions
      if (fdout == AT_FDCWD)
        rc = fchownat(cfd, catch, try->st.st_uid, try->st.st_gid,
                      AT_SYMLINK_NOFOLLOW);
      else rc = fchown(fdout, try->st.st_uid, try->st.st_gid);
      if (rc && !geteuid()) {
        char *pp;

        perror_msg("chown '%s'", pp = dirtree_path(try, 0));
        free(pp);
      }
    }

    // timestamp
    if (TT.pflags & _CP_timestamps) {
      struct timespec times[] = {try->st.st_atim, try->st.st_mtim};

      if (fdout == AT_FDCWD) utimensat(cfd, catch, times, AT_SYMLINK_NOFOLLOW);
      else futimens(fdout, times);
    }

    // mode comes last because other syscalls can strip suid bit
    if (fdout != AT_FDCWD) {
      if (TT.pflags & _CP_mode) fchmod(fdout, try->st.st_mode);
      xclose(fdout);
    }

    if (CFG_MV && toys.which->name[0] == 'm')
      if (unlinkat(tfd, try->name, S_ISDIR(try->st.st_mode) ? AT_REMOVEDIR :0))
        err = "%s";
  }

  if (err) {
    char *f = 0;

    if (catch == try->name) {
      f = dirtree_path(try, 0);
      while (try->parent) try = try->parent;
      catch = xmprintf("%s%s", TT.destname, f+strlen(try->name));
      free(f);
      f = catch;
    }
    perror_msg(err, catch);
    free(f);
  }
  return 0;
}

void cp_main(void)
{
  char *destname = toys.optargs[--toys.optc];
  int i, destdir = !stat(destname, &TT.top) && S_ISDIR(TT.top.st_mode);

  if (toys.optc>1 && !destdir) error_exit("'%s' not directory", destname);

  if (toys.optflags & (FLAG_a|FLAG_p)) {
    TT.pflags = CP_mode|CP_ownership|CP_timestamps;
    umask(0);
  }
  // Not using comma_args() (yet?) because interpeting as letters.
  if (CFG_CP_PRESERVE && (toys.optflags & FLAG_preserve)) {
    char *pre = xstrdup(TT.c.preserve), *s;

    if (comma_scan(pre, "all", 1)) TT.pflags = ~0;
    for (i=0; i<ARRAY_LEN(cp_preserve); i++)
      if (comma_scan(pre, cp_preserve[i].name, 1)) TT.pflags |= 1<<i;
    if (*pre) {

      // Try to interpret as letters, commas won't set anything this doesn't.
      for (s = TT.c.preserve; *s; s++) {
        for (i=0; i<ARRAY_LEN(cp_preserve); i++)
          if (*s == *cp_preserve[i].name) break;
        if (i == ARRAY_LEN(cp_preserve)) {
          if (*s == 'a') TT.pflags = ~0;
          else break;
        } else TT.pflags |= 1<<i;
      }

      if (*s) error_exit("bad --preserve=%s", pre);
    }
    free(pre);
  }
  if (!TT.callback) TT.callback = cp_node;

  // Loop through sources
  for (i=0; i<toys.optc; i++) {
    char *src = toys.optargs[i];
    int rc = 1;

    if (destdir) TT.destname = xmprintf("%s/%s", destname, basename(src));
    else TT.destname = destname;

    errno = EXDEV;
    if (CFG_MV && toys.which->name[0] == 'm') {
      int force = toys.optflags & FLAG_f, no_clobber = toys.optflags & FLAG_n;

      if (!force || no_clobber) {
        struct stat st;
        int exists = !stat(TT.destname, &st);

        // Prompt if -i or file isn't writable.  Technically "is writable" is
        // more complicated (022 is not writeable by the owner, just everybody
        // _else_) but I don't care.
        if (exists && ((toys.optflags & FLAG_i) || !(st.st_mode & 0222))) {
          fprintf(stderr, "%s: overwrite '%s'", toys.which->name, TT.destname);
          if (!yesno(1)) rc = 0;
          else unlink(TT.destname);
        }
        // if -n and dest exists, don't try to rename() or copy
        if (exists && no_clobber) rc = 0;
      }
      if (rc) rc = rename(src, TT.destname);
    }

    // Copy if we didn't mv, skipping nonexistent sources
    if (rc) {
      if (errno!=EXDEV || dirtree_flagread(src, DIRTREE_SHUTUP+
        DIRTREE_SYMFOLLOW*!!(toys.optflags&(FLAG_H|FLAG_L)), TT.callback))
          perror_msg("bad '%s'", src);
    }
    if (destdir) free(TT.destname);
  }
}

void mv_main(void)
{
  toys.optflags |= FLAG_d|FLAG_p|FLAG_R;

  cp_main();
}

// Export cp flags into install's flag context.

static inline int cp_flag_F(void) { return FLAG_F; };
static inline int cp_flag_p(void) { return FLAG_p; };
static inline int cp_flag_v(void) { return FLAG_v; };

// Switch to install's flag context
#define CLEANUP_cp
#define FOR_install
#include <generated/flags.h>

static int install_node(struct dirtree *try)
{
  try->st.st_mode = (TT.i.mode)
    ? string_to_mode(TT.i.mode, try->st.st_mode) : 0755;
  if (TT.i.group) try->st.st_gid = TT.gid;
  if (TT.i.user) try->st.st_uid = TT.uid;

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
      if (mkpathat(AT_FDCWD, *ss, 0777, 3)) perror_msg_raw(*ss);
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
  toys.optflags = cp_flag_F();
  if (flags & FLAG_v) toys.optflags |= cp_flag_v();
  if (flags & (FLAG_p|FLAG_o|FLAG_g)) toys.optflags |= cp_flag_p();

  if (TT.i.user) TT.uid = xgetuid(TT.i.user);
  if (TT.i.group) TT.gid = xgetgid(TT.i.group);

  TT.callback = install_node;
  cp_main();
}
