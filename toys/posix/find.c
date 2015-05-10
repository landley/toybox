/* find.c - Search directories for matching files.
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/find.c
 *
 * Our "unspecified" behavior for no paths is to use "."
 * Parentheses can only stack 4096 deep
 * Not treating two {} as an error, but only using last

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
    -name  PATTERN filename with wildcards   -iname      case insensitive -name
    -path  PATTERN path name with wildcards  -ipath      case insensitive -path
    -user  UNAME   belongs to user UNAME     -nouser     user not in /etc/passwd
    -group GROUP   belongs to group GROUP    -nogroup    group not in /etc/group
    -perm  [-]MODE permissons (-=at least)   -prune      ignore contents of dir
    -size  N[c]    512 byte blocks (c=bytes) -xdev       stay in this filesystem
    -links N       hardlink count            -atime N    accessed N days ago
    -ctime N       created N days ago        -mtime N    modified N days ago
    -newer FILE    newer mtime than FILE     -mindepth # at least # dirs down
    -depth         ignore contents of dir    -maxdepth # at most # dirs down
    -type [bcdflps] (block, char, dir, file, symlink, pipe, socket)

    Numbers N may be prefixed by a - (less than) or + (greater than):

    Combine matches with:
    !, -a, -o, ( )    not, and, or, group expressions

    Actions:
    -print   Print match with newline  -print0    Print match with null
    -exec    Run command with path     -execdir   Run command in file's dir
    -ok      Ask before exec           -okdir     Ask before execdir

    Commands substitute "{}" with matched file. End with ";" to run each file,
    or "+" (next argument after "{}") to collect and run with multiple files.
*/

#define FOR_find
#include "toys.h"

GLOBALS(
  char **filter;
  struct double_list *argdata;
  int topdir, xdev, depth, envsize;
  time_t now;
)

// None of this can go in TT because you can have more than one -exec
struct exec_range {
  char *next, *prev;

  int dir, plus, arglen, argsize, curly, namecount, namesize;
  char **argstart;
  struct double_list *names;
};

// Perform pending -exec (if any)
static int flush_exec(struct dirtree *new, struct exec_range *aa)
{
  struct double_list **dl;
  char **newargs;
  int rc = 0;

  if (!aa->namecount) return 0;

  if (aa->dir && new->parent) dl = (void *)&new->parent->extra;
  else dl = &aa->names;
  dlist_terminate(*dl);

  // switch to directory for -execdir, or back to top if we have an -execdir
  // _and_ a normal -exec, or are at top of tree in -execdir
  if (aa->dir && new->parent) rc = fchdir(new->parent->data);
  else if (TT.topdir != -1) rc = fchdir(TT.topdir);
  if (rc) {
    perror_msg("%s", new->name);

    return rc;
  }

  // execdir: accumulated execs in this directory's children.
  newargs = xmalloc(sizeof(char *)*(aa->arglen+aa->namecount+1));
  if (aa->curly < 0) {
    memcpy(newargs, aa->argstart, sizeof(char *)*aa->arglen);
    newargs[aa->arglen] = 0;
  } else {
    struct double_list *dl2 = *dl;
    int pos = aa->curly, rest = aa->arglen - aa->curly;

    // Collate argument list
    memcpy(newargs, aa->argstart, sizeof(char *)*pos);
    for (dl2 = *dl; dl2; dl2 = dl2->next) newargs[pos++] = dl2->data;
    rest = aa->arglen - aa->curly - 1;
    memcpy(newargs+pos, aa->argstart+aa->curly+1, sizeof(char *)*rest);
    newargs[pos+rest] = 0;
  }

  rc = xrun(newargs);

  llist_traverse(*dl, llist_free_double);
  *dl = 0;
  aa->namecount = 0;

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

char *strlower(char *s)
{
  char *try, *new;

  if (!CFG_TOYBOX_I18N) {
    try = new = xstrdup(s);
    for (; *s; s++) *(new++) = tolower(*s);
  } else {
    // I can't guarantee the string _won't_ expand during reencoding, so...?
    try = new = xmalloc(strlen(s)*2+1);

    while (*s) {
      wchar_t c;
      int len = mbrtowc(&c, s, MB_CUR_MAX, 0);

      if (len < 1) *(new++) = *(s++);
      else {
        s += len;
        // squash title case too
        c = towlower(c);

        // if we had a valid utf8 sequence, convert it to lower case, and can't
        // encode back to utf8, something is wrong with your libc. But just
        // in case somebody finds an exploit...
        len = wcrtomb(new, c, 0);
        if (len < 1) error_exit("bad utf8 %x", (int)c);
        new += len;
      }
    }
    *new = 0;
  }

  return try;
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
      if (!new->again) {
        struct dirtree *n;

        if (TT.depth) return recurse;
        for (n = new->parent; n; n = n->parent) {
          if (n->st.st_ino==new->st.st_ino && n->st.st_dev==new->st.st_dev) {
            error_msg("'%s': loop detected", s = dirtree_path(new, 0));
            free(s);

            return 0;
          }
        }
      } else {
        struct double_list *dl;

        if (TT.topdir != -1)
          for (dl = TT.argdata; dl; dl = dl->next)
            if (dl->prev == (void *)1 || !new->parent)
              toys.exitval |= flush_exec(new, (void *)dl);

        return 0;
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
    else if (!strcmp(s, "depth")) TT.depth = 1;
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
    } else if (!strcmp(s, "a") || !strcmp(s, "and")) {
      if (not) goto error;

    } else if (!strcmp(s, "print") || !strcmp("print0", s)) {
      print++;
      if (check) do_print(new, s[5] ? 0 : '\n');

    } else if (!strcmp(s, "nouser")) {
      if (check) if (getpwuid(new->st.st_uid)) test = 0;
    } else if (!strcmp(s, "nogroup")) {
      if (check) if (getgrgid(new->st.st_gid)) test = 0;
    } else if (!strcmp(s, "prune")) {
      if (check && S_ISDIR(new->st.st_dev) && !TT.depth) recurse = 0;

    // Remaining filters take an argument
    } else {
      if (!strcmp(s, "name") || !strcmp(s, "iname")
        || !strcmp(s, "path") || !strcmp(s, "ipath"))
      {
        int i = (*s == 'i');
        char *arg = ss[1], *path = 0, *name = new->name;

        // Handle path expansion and case flattening
        if (new && s[i] == 'p') name = path = dirtree_path(new, 0);
        if (i) {
          if (check || !new) {
            name = strlower(new ? name : arg);
            if (!new) {
              dlist_add(&TT.argdata, name);
              free(path);
            } else arg = ((struct double_list *)llist_pop(&argdata))->data;
          }
        }

        if (check) {
          test = !fnmatch(arg, name, FNM_PATHNAME*(s[i] == 'p'));
          free(path);
          if (i) free(name);
        }
      } else if (!strcmp(s, "perm")) {
        if (check) {
          char *m = ss[1];
          mode_t m1 = string_to_mode(m+(*m == '-'), 0),
                 m2 = new->st.st_dev & 07777;

          if (*m != '-') m2 &= m1;
          test = m1 == m2;
        }
      } else if (!strcmp(s, "type")) {
        if (check) {
          int types[] = {S_IFBLK, S_IFCHR, S_IFDIR, S_IFLNK, S_IFIFO,
                         S_IFREG, S_IFSOCK}, i = stridx("bcdlpfs", *ss[1]);

          if (i<0) error_exit("bad -type '%c'", *ss[1]);
          if ((new->st.st_mode & S_IFMT) != types[i]) test = 0;
        }

      } else if (!strcmp(s, "atime")) {
        if (check)
          test = compare_numsign(TT.now - new->st.st_atime, 86400, ss[1]);
      } else if (!strcmp(s, "ctime")) {
        if (check)
          test = compare_numsign(TT.now - new->st.st_ctime, 86400, ss[1]);
      } else if (!strcmp(s, "mtime")) {
        if (check)
          test = compare_numsign(TT.now - new->st.st_mtime, 86400, ss[1]);
      } else if (!strcmp(s, "size")) {
        if (check)
          test = compare_numsign(new->st.st_size, 512, ss[1]);
      } else if (!strcmp(s, "links")) {
        if (check) test = compare_numsign(new->st.st_nlink, 0, ss[1]);
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

        if (!new && ss[1]) {
          udl = xmalloc(sizeof(*udl));
          dlist_add_nomalloc(&TT.argdata, (void *)udl);

          if (*s == 'u') udl->u.uid = xgetpwnamid(ss[1])->pw_uid;
          else if (*s == 'g') udl->u.gid = xgetgrnamid(ss[1])->gr_gid;
          else {
            struct stat st;

            xstat(ss[1], &st);
            udl->u.tm = st.st_mtim;
          }
        } else if (check) {
          udl = (void *)llist_pop(&argdata);
          if (*s == 'u') test = new->st.st_uid == udl->u.uid;
          else if (*s == 'g') test = new->st.st_gid == udl->u.gid;
          else {
            test = new->st.st_mtim.tv_sec > udl->u.tm.tv_sec;
            if (new->st.st_mtim.tv_sec == udl->u.tm.tv_sec)
              test = new->st.st_mtim.tv_nsec > udl->u.tm.tv_nsec;
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
              if (!strcmp(ss[len+1], "+")) {

                // Measure environment space
                if (!TT.envsize) {
                  char **env;

                  for (env = environ; *env; env++)
                    TT.envsize += sizeof(char *) + strlen(*env) + 1;
                  TT.envsize += sizeof(char *);
                }
                aa->plus++;
                len++;
                break;
              }
            } else aa->argsize += sizeof(char *) + strlen(ss[len]) + 1;
          }
          if (!ss[len]) error_exit("-exec without \\;");
          ss += len;
          aa->arglen = len;
          aa->dir = !!strchr(s, 'd');
          if (aa->dir && TT.topdir == -1) TT.topdir = xopen(".", 0);

        // collect names and execute commands
        } else {
          char *name, *ss1 = ss[1];
          struct double_list **ddl;

          // Grab command line exec argument list
          aa = (void *)llist_pop(&argdata);
          ss += aa->arglen + 1;

          if (!check) goto cont;
          // name is always a new malloc, so we can always free it.
          name = aa->dir ? xstrdup(new->name) : dirtree_path(new, 0);

          // Mark entry so COMEAGAIN can call flush_exec() in parent.
          // This is never a valid pointer value for prev to have otherwise
          if (aa->dir) aa->prev = (void *)1;

          if (*s == 'o') {
            char *prompt = xmprintf("[%s] %s", ss1, name);
            test = yesno(prompt, 0);
            free(prompt);
            if (!test) {
              free(name);
              goto cont;
            }
          }

          // Add next name to list (global list without -dir, local with)
          if (aa->dir && new->parent)
            ddl = (struct double_list **)&new->parent->extra;
          else ddl = &aa->names;

          // Is this + mode?
          if (aa->plus) {
            int size = sizeof(char *)+strlen(name)+1;

            // Linux caps environment space (env vars + args) at 32 4k pages.
            // todo: is there a way to probe this instead of constant here?

            if (TT.envsize+aa->argsize+aa->namesize+size >= 131072)
              toys.exitval |= flush_exec(new, aa);
            aa->namesize += size;
          }
          dlist_add(ddl, name);
          aa->namecount++;
          if (!aa->plus) test = flush_exec(new, aa);
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
    dirtree_handle_callback(dirtree_start(ss[i], toys.optflags&(FLAG_H|FLAG_L)),
      do_find);

  if (CFG_TOYBOX_FREE) {
    close(TT.topdir);
    llist_traverse(TT.argdata, free);
  }
}
