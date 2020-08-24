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

USE_CP(NEWTOY(cp, "<2(preserve):;D(parents)RHLPprdaslvnF(remove-destination)fiT[-HLPd][-ni]", TOYFLAG_BIN))
USE_MV(NEWTOY(mv, "<2vnF(remove-destination)fiT[-ni]", TOYFLAG_BIN))
USE_INSTALL(NEWTOY(install, "<1cdDpsvm:o:g:", TOYFLAG_USR|TOYFLAG_BIN))

config CP
  bool "cp"
  default y
  help
    usage: cp [-adfHiLlnPpRrsTv] [--preserve=motcxa] SOURCE... DEST

    Copy files from SOURCE to DEST.  If more than one SOURCE, DEST must
    be a directory.

    -a	Same as -dpr
    -D	Create leading dirs under DEST (--parents)
    -d	Don't dereference symlinks
    -F	Delete any existing destination file first (--remove-destination)
    -f	Delete destination files we can't write to
    -H	Follow symlinks listed on command line
    -i	Interactive, prompt before overwriting existing DEST
    -L	Follow all symlinks
    -l	Hard link instead of copy
    -n	No clobber (don't overwrite DEST)
    -P	Do not follow symlinks
    -p	Preserve timestamps, ownership, and mode
    -R	Recurse into subdirectories (DEST must be a directory)
    -r	Synonym for -R
    -s	Symlink instead of copy
    -T	DEST always treated as file, max 2 arguments
    -v	Verbose

    Arguments to --preserve are the first letter(s) of:

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
    usage: mv [-finTv] SOURCE... DEST

    -f	Force copy by deleting destination file
    -i	Interactive, prompt before overwriting existing DEST
    -n	No clobber (don't overwrite DEST)
    -T	DEST always treated as file, max 2 arguments
    -v	Verbose

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
    // install's options
    struct {
      char *g, *o, *m;
    } i;
    // cp's options
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
        if (!yesno(0)) return 0;
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
              return DIRTREE_COMEAGAIN | (DIRTREE_SYMFOLLOW*!!FLAG(L));

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
                 && (try->parent || (flags & (FLAG_a|FLAG_P|FLAG_r))))
      {
        int i;

        // make symlink, or make block/char/fifo/socket
        if (S_ISLNK(try->st.st_mode)
            ? ((i = readlinkat0(tfd, try->name, toybuf, sizeof(toybuf))) &&
               ((!unlinkat(cfd, catch, 0) || ENOENT == errno) &&
                !symlinkat(toybuf, cfd, catch)))
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
        // When copying contents use symlink target's attributes
        if (S_ISLNK(try->st.st_mode)) fstat(fdin, &try->st);
        fdout = openat(cfd, catch, O_RDWR|O_CREAT|O_TRUNC, try->st.st_mode);
        if (fdout >= 0) {
          xsendfile(fdin, fdout);
          err = 0;
        }

        // We only copy xattrs for files because there's no flistxattrat()
        if (TT.pflags&(_CP_xattr|_CP_context)) {
          ssize_t listlen = xattr_flist(fdin, 0, 0), len;
          char *name, *value, *list;

          if (listlen>0) {
            list = xmalloc(listlen);
            xattr_flist(fdin, list, listlen);
            list[listlen-1] = 0; // I do not trust this API.
            for (name = list; name-list < listlen; name += strlen(name)+1) {
              if (!(TT.pflags&_CP_xattr) && strncmp(name, "security.", 9))
                continue;
              if ((len = xattr_fget(fdin, name, 0, 0))>0) {
                value = xmalloc(len);
                if (len == xattr_fget(fdin, name, value, len))
                  if (xattr_fset(fdout, name, value, len, 0))
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

  if (FLAG(T)) {
    if (toys.optc>1) help_exit("Max 2 arguments");
    if (destdir) error_exit("'%s' is a directory", destname);
  }

  if ((toys.optc>1 || FLAG(D)) && !destdir)
    error_exit("'%s' not directory", destname);

  if (FLAG(a)||FLAG(p)) TT.pflags = _CP_mode|_CP_ownership|_CP_timestamps;

  // Not using comma_args() (yet?) because interpeting as letters.
  if (FLAG(preserve)) {
    char *pre = xstrdup(TT.c.preserve ? TT.c.preserve : "mot"), *s;

    if (comma_remove(pre, "all")) TT.pflags = ~0;
    for (i=0; i<ARRAY_LEN(cp_preserve); i++)
      while (comma_remove(pre, cp_preserve[i].name)) TT.pflags |= 1<<i;
    if (*pre) {

      // Try to interpret as letters, commas won't set anything this doesn't.
      for (s = pre; *s; s++) {
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
  if (TT.pflags & _CP_mode) umask(0);
  if (!TT.callback) TT.callback = cp_node;

  // Loop through sources
  for (i=0; i<toys.optc; i++) {
    char *src = toys.optargs[i], *trail = src;
    int rc = 1;

    while (*++trail);
    if (*--trail == '/') *trail = 0;

    if (destdir) {
      char *s = FLAG(D) ? src : getbasename(src);

      TT.destname = xmprintf("%s/%s", destname, s);
      if (FLAG(D)) {
        if (!(s = fileunderdir(TT.destname, destname))) {
          error_msg("%s not under %s", TT.destname, destname);
          continue;
        }
        // TODO: .. follows abspath, not links...
        free(s);
        mkpath(TT.destname);
      }
    } else TT.destname = destname;

    errno = EXDEV;
    if (CFG_MV && toys.which->name[0] == 'm') {
      int force = FLAG(f), no_clobber = FLAG(n);

      if (!force || no_clobber) {
        struct stat st;
        int exists = !stat(TT.destname, &st);

        // Prompt if -i or file isn't writable.  Technically "is writable" is
        // more complicated (022 is not writeable by the owner, just everybody
        // _else_) but I don't care.
        if (exists && (FLAG(i) || (!(st.st_mode & 0222) && isatty(0)))) {
          fprintf(stderr, "%s: overwrite '%s'", toys.which->name, TT.destname);
          if (!yesno(0)) rc = 0;
          else unlink(TT.destname);
        }
        // if -n and dest exists, don't try to rename() or copy
        if (exists && no_clobber) rc = 0;
      }
      if (rc) rc = rename(src, TT.destname);
      if (errno && !*trail) *trail = '/';
    }

    // Copy if we didn't mv, skipping nonexistent sources
    if (rc) {
      if (errno!=EXDEV || dirtree_flagread(src, DIRTREE_SHUTUP+
        DIRTREE_SYMFOLLOW*!!(FLAG(H)||FLAG(L)), TT.callback))
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
  try->st.st_mode = TT.i.m ? string_to_mode(TT.i.m, try->st.st_mode) : 0755;
  if (TT.i.g) try->st.st_gid = TT.gid;
  if (TT.i.o) try->st.st_uid = TT.uid;

  // Always returns 0 because no -r
  cp_node(try);

  // No -r so always one level deep, so destname as set by cp_node() is correct
  if (FLAG(s) && xrun((char *[]){"strip", "-p", TT.destname, 0}))
    toys.exitval = 1;

  return 0;
}

void install_main(void)
{
  char **ss;

  TT.uid = TT.i.o ? xgetuid(TT.i.o) : -1;
  TT.gid = TT.i.g ? xgetgid(TT.i.g) : -1;

  if (FLAG(d)) {
    for (ss = toys.optargs; *ss; ss++) {
      if (FLAG(v)) printf("%s\n", *ss);
      if (mkpathat(AT_FDCWD, *ss, 0777, MKPATHAT_MKLAST | MKPATHAT_MAKE))
        perror_msg_raw(*ss);
      if (FLAG(g)||FLAG(o))
        if (lchown(*ss, TT.uid, TT.gid)) perror_msg("chown '%s'", *ss);
    }

    return;
  }

  if (FLAG(D)) {
    TT.destname = toys.optargs[toys.optc-1];
    if (mkpathat(AT_FDCWD, TT.destname, 0, MKPATHAT_MAKE))
      perror_exit("-D '%s'", TT.destname);
    if (toys.optc == 1) return;
  }
  if (toys.optc < 2) error_exit("needs 2 args");

  // Translate flags from install to cp
  toys.optflags = cp_flag_F() + cp_flag_v()*!!FLAG(v)
    + cp_flag_p()*!!(FLAG(p)|FLAG(o)|FLAG(g));

  TT.callback = install_node;
  cp_main();
}
