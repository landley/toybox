/* find.c - Search directories for matching files.
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/find.c
 *
 * Our "unspecified" behavior for no paths is to use "."
 * Parentheses can only stack 4096 deep
 * Not treating two {} as an error, but only using last
 *
 * TODO: -empty (dirs too!)

USE_FIND(NEWTOY(find, "?^HL[-HL]", TOYFLAG_USR|TOYFLAG_BIN))

config FIND
  bool "find"
  default y
  help
    usage: find [-HL] [DIR...] [<options>]

    Search directories for matching files.
    Default: search "." match all -print all matches.

    -H  Follow command line symlinks         -L  Follow all symlinks

    Match filters:
    -name  PATTERN  filename with wildcards   -iname      case insensitive -name
    -path  PATTERN  path name with wildcards  -ipath      case insensitive -path
    -user  UNAME    belongs to user UNAME     -nouser     user ID not known
    -group GROUP    belongs to group GROUP    -nogroup    group ID not known
    -perm  [-/]MODE permissions (-=min /=any) -prune      ignore contents of dir
    -size  N[c]     512 byte blocks (c=bytes) -xdev       only this filesystem
    -links N        hardlink count            -atime N[u] accessed N units ago
    -ctime N[u]     created N units ago       -mtime N[u] modified N units ago
    -newer FILE     newer mtime than FILE     -mindepth # at least # dirs down
    -depth          ignore contents of dir    -maxdepth # at most # dirs down
    -inum  N        inode number N            -empty      empty files and dirs
    -type [bcdflps] (block, char, dir, file, symlink, pipe, socket)

    Numbers N may be prefixed by a - (less than) or + (greater than). Units for
    -Xtime are d (days, default), h (hours), m (minutes), or s (seconds).

    Combine matches with:
    !, -a, -o, ( )    not, and, or, group expressions

    Actions:
    -print   Print match with newline  -print0    Print match with null
    -exec    Run command with path     -execdir   Run command in file's dir
    -ok      Ask before exec           -okdir     Ask before execdir
    -delete  Remove matching file/dir

    Commands substitute "{}" with matched file. End with ";" to run each file,
    or "+" (next argument after "{}") to collect and run with multiple files.
*/

#define FOR_find
#include "toys.h"

GLOBALS(
  char **filter;
  struct double_list *argdata;
  int topdir, xdev, depth;
  time_t now;
  long max_bytes;
)

struct execdir_data {
  struct execdir_data *next;

  int namecount;
  struct double_list *names;
};

// None of this can go in TT because you can have more than one -exec
struct exec_range {
  char *next, *prev;  // layout compatible with struct double_list

  int dir, plus, arglen, argsize, curly;
  char **argstart;
  struct execdir_data exec, *execdir;
};

// Perform pending -exec (if any)
static int flush_exec(struct dirtree *new, struct exec_range *aa)
{
  struct execdir_data *bb = aa->execdir ? aa->execdir : &aa->exec;
  char **newargs;
  int rc, revert = 0;

  if (!bb->namecount) return 0;

  dlist_terminate(bb->names);

  // switch to directory for -execdir, or back to top if we have an -execdir
  // _and_ a normal -exec, or are at top of tree in -execdir
  if (TT.topdir != -1) {
    if (aa->dir && new && new->parent) {
      revert++;
      rc = fchdir(new->parent->dirfd);
    } else rc = fchdir(TT.topdir);
    if (rc) {
      perror_msg_raw(revert ? new->name : ".");

      return rc;
    }
  }

  // execdir: accumulated execs in this directory's children.
  newargs = xmalloc(sizeof(char *)*(aa->arglen+bb->namecount+1));
  if (aa->curly < 0) {
    memcpy(newargs, aa->argstart, sizeof(char *)*aa->arglen);
    newargs[aa->arglen] = 0;
  } else {
    int pos = aa->curly, rest = aa->arglen - aa->curly;
    struct double_list *dl;

    // Collate argument list
    memcpy(newargs, aa->argstart, sizeof(char *)*pos);
    for (dl = bb->names; dl; dl = dl->next) newargs[pos++] = dl->data;
    rest = aa->arglen - aa->curly - 1;
    memcpy(newargs+pos, aa->argstart+aa->curly+1, sizeof(char *)*rest);
    newargs[pos+rest] = 0;
  }

  rc = xrun(newargs);

  llist_traverse(bb->names, llist_free_double);
  bb->names = 0;
  bb->namecount = 0;

  if (revert) revert = fchdir(TT.topdir);

  return rc;
}

// Return numeric value with explicit sign
static int compare_numsign(long val, long units, char *str)
{
  char sign = 0;
  long myval;

  if (*str == '+' || *str == '-') sign = *(str++);
  else if (!isdigit(*str)) error_exit("%s not [+-]N", str);
  myval = atolx(str);
  if (units && isdigit(str[strlen(str)-1])) myval *= units;

  if (sign == '+') return val > myval;
  if (sign == '-') return val < myval;
  return val == myval;
}

static void do_print(struct dirtree *new, char c)
{
  char *s=dirtree_path(new, 0);

  xprintf("%s%c", s, c);
  free(s);
}

// Descend or ascend -execdir + directory level
static void execdir(struct dirtree *new, int flush)
{
  struct double_list *dl;
  struct exec_range *aa;
  struct execdir_data *bb;

  if (new && TT.topdir == -1) return;

  for (dl = TT.argdata; dl; dl = dl->next) {
    if (dl->prev != (void *)1) continue;
    aa = (void *)dl;
    if (!aa->plus || (new && !aa->dir)) continue;

    if (flush) {

      // Flush pending "-execdir +" instances for this dir
      // or flush everything for -exec at top
      toys.exitval |= flush_exec(new, aa);

      // pop per-directory struct
      if ((bb = aa->execdir)) {
        aa->execdir = bb->next;
        free(bb);
      }
    } else if (aa->dir) {

      // Push new per-directory struct for -execdir/okdir + codepath. (Can't
      // use new->extra because command line may have multiple -execdir)
      bb = xzalloc(sizeof(struct execdir_data));
      bb->next = aa->execdir;
      aa->execdir = bb;
    }
  }
} 

// Call this with 0 for first pass argument parsing and syntax checking (which
// populates argdata). Later commands traverse argdata (in order) when they
// need "do once" results.
static int do_find(struct dirtree *new)
{
  int pcount = 0, print = 0, not = 0, active = !!new, test = active, recurse;
  struct double_list *argdata = TT.argdata;
  char *s, **ss;

  recurse = DIRTREE_COMEAGAIN|(DIRTREE_SYMFOLLOW*!!(toys.optflags&FLAG_L));

  // skip . and .. below topdir, handle -xdev and -depth
  if (new) {
    if (new->parent) {
      if (!dirtree_notdotdot(new)) return 0;
      if (TT.xdev && new->st.st_dev != new->parent->st.st_dev) recurse = 0;
    }

    if (S_ISDIR(new->st.st_mode)) {
      // Descending into new directory
      if (!new->again) {
        struct dirtree *n;

        for (n = new->parent; n; n = n->parent) {
          if (n->st.st_ino==new->st.st_ino && n->st.st_dev==new->st.st_dev) {
            error_msg("'%s': loop detected", s = dirtree_path(new, 0));
            free(s);

            return 0;
          }
        }

        if (TT.depth) {
          execdir(new, 0);

          return recurse;
        }
      // Done with directory (COMEAGAIN call)
      } else {
        execdir(new, 1);
        recurse = 0;
        if (!TT.depth) return 0;
      }
    }
  }

  // pcount: parentheses stack depth (using toybuf bytes, 4096 max depth)
  // test: result of most recent test
  // active: if 0 don't perform tests
  // not: a pending ! applies to this test (only set if performing tests)
  // print: saw one of print/ok/exec, no need for default -print

  if (TT.filter) for (ss = TT.filter; *ss; ss++) {
    int check = active && test;

    s = *ss;

    // handle ! ( ) using toybuf as a stack
    if (*s != '-') {
      if (s[1]) goto error;

      if (*s == '!') {
        // Don't invert if we're not making a decision
        if (check) not = !not;

      // Save old "not" and "active" on toybuf stack.
      // Deactivate this parenthetical if !test
      // Note: test value should never change while !active
      } else if (*s == '(') {
        if (pcount == sizeof(toybuf)) goto error;
        toybuf[pcount++] = not+(active<<1);
        if (!check) active = 0;
        not = 0;

      // Pop status, apply deferred not to test
      } else if (*s == ')') {
        if (--pcount < 0) goto error;
        // Pop active state, apply deferred not (which was only set if checking)
        active = (toybuf[pcount]>>1)&1;
        if (active && (toybuf[pcount]&1)) test = !test;
        not = 0;
      } else goto error;

      continue;
    } else s++;

    if (!strcmp(s, "xdev")) TT.xdev = 1;
    else if (!strcmp(s, "delete")) {
      // Delete forces depth first
      TT.depth = 1;
      if (new && check)
        test = !unlinkat(dirtree_parentfd(new), new->name,
          S_ISDIR(new->st.st_mode) ? AT_REMOVEDIR : 0);
    } else if (!strcmp(s, "depth")) TT.depth = 1;
    else if (!strcmp(s, "o") || !strcmp(s, "or")) {
      if (not) goto error;
      if (active) {
        if (!test) test = 1;
        else active = 0;     // decision has been made until next ")"
      }
    } else if (!strcmp(s, "not")) {
      if (check) not = !not;
      continue;
    // Mostly ignore NOP argument
    } else if (!strcmp(s, "a") || !strcmp(s, "and") || !strcmp(s, "noleaf")) {
      if (not) goto error;

    } else if (!strcmp(s, "print") || !strcmp("print0", s)) {
      print++;
      if (check) do_print(new, s[5] ? 0 : '\n');

    } else if (!strcmp(s, "nouser")) {
      if (check) if (bufgetpwuid(new->st.st_uid)) test = 0;
    } else if (!strcmp(s, "nogroup")) {
      if (check) if (bufgetgrgid(new->st.st_gid)) test = 0;
    } else if (!strcmp(s, "prune")) {
      if (check && S_ISDIR(new->st.st_mode) && !TT.depth) recurse = 0;

    // Remaining filters take an argument
    } else {
      if (!strcmp(s, "name") || !strcmp(s, "iname")
        || !strcmp(s, "path") || !strcmp(s, "ipath"))
      {
        int i = (*s == 'i');
        char *arg = ss[1], *path = 0, *name = new ? new->name : arg;

        // Handle path expansion and case flattening
        if (new && s[i] == 'p') name = path = dirtree_path(new, 0);
        if (i) {
          if ((check || !new) && name) name = strlower(name);
          if (!new) dlist_add(&TT.argdata, name);
          else arg = ((struct double_list *)llist_pop(&argdata))->data;
        }

        if (check) {
          test = !fnmatch(arg, name, FNM_PATHNAME*(s[i] == 'p'));
          if (i) free(name);
        }
        free(path);
      } else if (!strcmp(s, "perm")) {
        if (check) {
          char *m = ss[1];
          int match_min = *m == '-',
              match_any = *m == '/';
          mode_t m1 = string_to_mode(m+(match_min || match_any), 0),
                 m2 = new->st.st_mode & 07777;

          if (match_min || match_any) m2 &= m1;
          test = match_any ? !m1 || m2 : m1 == m2;
        }
      } else if (!strcmp(s, "type")) {
        if (check) {
          int types[] = {S_IFBLK, S_IFCHR, S_IFDIR, S_IFLNK, S_IFIFO,
                         S_IFREG, S_IFSOCK}, i = stridx("bcdlpfs", *ss[1]);

          if (i<0) error_exit("bad -type '%c'", *ss[1]);
          if ((new->st.st_mode & S_IFMT) != types[i]) test = 0;
        }

      } else if (strchr("acm", *s)
        && (!strcmp(s+1, "time") || !strcmp(s+1, "min")))
      {
        if (check) {
          char *copy = ss[1];
          time_t thyme = (int []){new->st.st_atime, new->st.st_ctime,
                                  new->st.st_mtime}[stridx("acm", *s)];
          int len = strlen(copy), uu, units = (s[1]=='m') ? 60 : 86400;

          if (len && -1!=(uu = stridx("dhms",tolower(copy[len-1])))) {
            copy = xstrdup(copy);
            copy[--len] = 0;
            units = (int []){86400, 3600, 60, 1}[uu];
          }
          test = compare_numsign(TT.now - thyme, units, copy);
          if (copy != ss[1]) free(copy);
        }
      } else if (!strcmp(s, "size")) {
        if (check)
          test = compare_numsign(new->st.st_size, 512, ss[1]);
      } else if (!strcmp(s, "links")) {
        if (check) test = compare_numsign(new->st.st_nlink, 0, ss[1]);
      } else if (!strcmp(s, "inum")) {
        if (check)
          test = compare_numsign(new->st.st_ino, 0, ss[1]);
      } else if (!strcmp(s, "mindepth") || !strcmp(s, "maxdepth")) {
        if (check) {
          struct dirtree *dt = new;
          int i = 0, d = atolx(ss[1]);

          while ((dt = dt->parent)) i++;
          if (s[1] == 'i') {
            test = i >= d;
            if (i == d && not) recurse = 0;
          } else {
            test = i <= d;
            if (i == d && !not) recurse = 0;
          }
        }
      } else if (!strcmp(s, "user") || !strcmp(s, "group")
              || !strcmp(s, "newer"))
      {
        struct {
          void *next, *prev;
          union {
            uid_t uid;
            gid_t gid;
            struct timespec tm;
          } u;
        } *udl;

        if (!new) {
          if (ss[1]) {
            udl = xmalloc(sizeof(*udl));
            dlist_add_nomalloc(&TT.argdata, (void *)udl);

            if (*s == 'u') udl->u.uid = xgetuid(ss[1]);
            else if (*s == 'g') udl->u.gid = xgetgid(ss[1]);
            else {
              struct stat st;

              xstat(ss[1], &st);
              udl->u.tm = st.st_mtim;
            }
          }
        } else {
          udl = (void *)llist_pop(&argdata);
          if (check) {
            if (*s == 'u') test = new->st.st_uid == udl->u.uid;
            else if (*s == 'g') test = new->st.st_gid == udl->u.gid;
            else {
              test = new->st.st_mtim.tv_sec > udl->u.tm.tv_sec;
              if (new->st.st_mtim.tv_sec == udl->u.tm.tv_sec)
                test = new->st.st_mtim.tv_nsec > udl->u.tm.tv_nsec;
            }
          }
        }
      } else if (!strcmp(s, "exec") || !strcmp("ok", s)
              || !strcmp(s, "execdir") || !strcmp(s, "okdir"))
      {
        struct exec_range *aa;

        print++;

        // Initial argument parsing pass
        if (!new) {
          int len;

          // catch "-exec" with no args and "-exec \;"
          if (!ss[1] || !strcmp(ss[1], ";")) error_exit("'%s' needs 1 arg", s);

          dlist_add_nomalloc(&TT.argdata, (void *)(aa = xzalloc(sizeof(*aa))));
          aa->argstart = ++ss;
          aa->curly = -1;

          // Record command line arguments to -exec
          for (len = 0; ss[len]; len++) {
            if (!strcmp(ss[len], ";")) break;
            else if (!strcmp(ss[len], "{}")) {
              aa->curly = len;
              if (ss[len+1] && !strcmp(ss[len+1], "+")) {
                aa->plus++;
                len++;
                break;
              }
            } else aa->argsize += sizeof(char *) + strlen(ss[len]) + 1;
          }
          if (!ss[len]) error_exit("-exec without %s",
            aa->curly!=-1 ? "\\;" : "{}");
          ss += len;
          aa->arglen = len;
          aa->dir = !!strchr(s, 'd');
          if (TT.topdir == -1) TT.topdir = xopenro(".");

        // collect names and execute commands
        } else {
          char *name, *ss1 = ss[1];
          struct execdir_data *bb;

          // Grab command line exec argument list
          aa = (void *)llist_pop(&argdata);
          ss += aa->arglen + 1;

          if (!check) goto cont;
          // name is always a new malloc, so we can always free it.
          name = aa->dir ? xstrdup(new->name) : dirtree_path(new, 0);

          if (*s == 'o') {
            fprintf(stderr, "[%s] %s", ss1, name);
            if (!(test = yesno(0))) {
              free(name);
              goto cont;
            }
          }

          // Add next name to list (global list without -dir, local with)
          bb = aa->execdir ? aa->execdir : &aa->exec;
          dlist_add(&bb->names, name);
          bb->namecount++;

          // -exec + collates and saves result in exitval
          if (aa->plus) {
            // Mark entry so COMEAGAIN can call flush_exec() in parent.
            // This is never a valid pointer value for prev to have otherwise
            // Done here vs argument parsing pass so it's after dlist_terminate
            aa->prev = (void *)1;

            // Flush if the child's environment space gets too large.
            // An insanely long path (>2 gigs) could wrap the counter and
            // defeat this test, which could potentially trigger OOM killer.
            if ((aa->plus += sizeof(char *)+strlen(name)+1) > TT.max_bytes) {
              aa->plus = 1;
              toys.exitval |= flush_exec(new, aa);
            }
          } else test = flush_exec(new, aa);
        }

        // Argument consumed, skip the check.
        goto cont;
      } else goto error;

      // This test can go at the end because we do a syntax checking
      // pass first. Putting it here gets the error message (-unknown
      // vs -known noarg) right.
      if (!*++ss) error_exit("'%s' needs 1 arg", --s);
    }
cont:
    // Apply pending "!" to result
    if (active && not) test = !test;
    not = 0;
  }

  if (new) {
    // If there was no action, print
    if (!print && test) do_print(new, '\n');

    if (S_ISDIR(new->st.st_mode)) execdir(new, 0);
 
  } else dlist_terminate(TT.argdata);

  return recurse;

error:
  error_exit("bad arg '%s'", *ss);
}

void find_main(void)
{
  int i, len;
  char **ss = toys.optargs;

  TT.topdir = -1;
  TT.max_bytes = sysconf(_SC_ARG_MAX) - environ_bytes();

  // Distinguish paths from filters
  for (len = 0; toys.optargs[len]; len++)
    if (strchr("-!(", *toys.optargs[len])) break;
  TT.filter = toys.optargs+len;

  // use "." if no paths
  if (!len) {
    ss = (char *[]){"."};
    len = 1;
  }

  // first pass argument parsing, verify args match up, handle "evaluate once"
  TT.now = time(0);
  do_find(0);

  // Loop through paths
  for (i = 0; i < len; i++)
    dirtree_flagread(ss[i], DIRTREE_SYMFOLLOW*!!(toys.optflags&(FLAG_H|FLAG_L)),
      do_find);

  execdir(0, 1);

  if (CFG_TOYBOX_FREE) {
    close(TT.topdir);
    llist_traverse(TT.argdata, free);
  }
}
