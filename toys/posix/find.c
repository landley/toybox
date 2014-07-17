/* find.c - Search directories for matching files.
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/find.c
 * Our "unspecified" behavior for no paths is to use "."
 * Parentheses can only stack 4096 deep

USE_FIND(NEWTOY(find, "^HL", TOYFLAG_USR|TOYFLAG_BIN))

config FIND
  bool "find"
  default n
  help
    usage: find [-HL] [DIR...] [<options>]

    Search directories for matching files.
    (Default is to search "." match all and display matches.)

    -H  Follow command line symlinks
    -L  Follow all symlinks

    Match filters:
    -name <pattern>    filename (with wildcards)
    -path <pattern>    path name (with wildcards)
    -nouser            belongs to unknown user
    -nogroup           belongs to unknown group
    -xdev              do not cross into new filesystems
    -prune             do not descend into children
    -perm  MODE        permissons (prefixed with - means at least these)
    -iname PATTERN     case insensitive filename
    -links N           hardlink count
    -user  UNAME       belongs to user
    -group GROUP       belongs to group
    -size  N[c]
    -atime N
    -ctime N
    -type  [bcdflps]   type (block, char, dir, file, symlink, pipe, socket)
    -mtime N           last modified N (24 hour) days ago

    Numbers N may be prefixed by a - (less than) or + (greater than)

    Combine matches with:
    !, -a, -o, ( )    not, and, or, group expressions


    Actions:
    -exec
    -print
    -print0
*/

// find . ! \( -name blah -print \)
// find . -o

#define FOR_find
#include "toys.h"

GLOBALS(
  char **filter;
  struct double_list *argdata;
  int xdev, depth;
  time_t now;
)

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

void todo_store_argument(void)
{
  error_exit("NOP");
}

// pending issues:
// old false -a ! new false does not yield true.
// 
// -user -group -newer evaluate once and save result (where?)
// add -print if no action (-exec, -ok, -print)
// find . -print -xdev (should xdev before print)

// Call this with 0 for first pass argument parsing, syntax checking.
static int do_find(struct dirtree *new)
{
  int pcount = 0, print = 0, not = 0, active = !!new, test = active, recurse;
  char *s, **ss;

  recurse = DIRTREE_RECURSE|((toys.optflags&FLAG_L) ? DIRTREE_SYMFOLLOW : 0);

  // skip . and .. below topdir, handle -xdev and -depth
  if (active) {
    if (new->parent) {
      if (!dirtree_notdotdot(new)) return 0;
      if (TT.xdev && new->st.st_dev != new->parent->st.st_dev) return 0;
    }
    if (TT.depth && S_ISDIR(new->st.st_dev) && new->data != -1)
      return DIRTREE_COMEAGAIN;
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

    // Mostly ignore NOP argument
    } else if (!strcmp(s, "a") || !strcmp(s, "and")) {
      if (not) goto error;

    } else if (!strcmp(s, "print") || !strcmp("print0", s)) {
      print++;
      if (check) do_print(new, s[6] ? 0 : '\n');

    } else if (!strcmp(s, "nouser")) {
      if (check) if (getpwuid(new->st.st_uid)) test = 0;
    } else if (!strcmp(s, "nogroup")) {
      if (check) if (getgrgid(new->st.st_gid)) test = 0;
    } else if (!strcmp(s, "prune")) {
      if (check && S_ISDIR(new->st.st_dev) && !TT.depth) recurse = 0;

    // Remaining filters take an argument
    } else {

      if (!strcmp(s, "name") || !strcmp(s, "iname")) {
        if (check) {
          if (*s == 'i') todo_store_argument();
//            if (!new) {
//            } else {
//              name = xstrdup(name);
//              while (
          test = !fnmatch(ss[1], new->name, 0);
        }
      } else if (!strcmp(s, "path")) {
        if (check) {
          char *path = dirtree_path(new, 0);
          int len = strlen(ss[1]);

          if (strncmp(path, ss[1], len) || (ss[1][len] && ss[1][len] != '/'))
            test = 0;
          free(s);
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
          char c = stridx("bcdlpfs", *ss[1]);
          int types[] = {S_IFBLK, S_IFCHR, S_IFDIR, S_IFLNK, S_IFIFO,
                         S_IFREG, S_IFSOCK};

          if ((new->st.st_dev & S_IFMT) != types[c]) test = 0;
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
      } else if (!strcmp(s, "user")) {
        todo_store_argument();
      } else if (!strcmp(s, "group")) {
        todo_store_argument();
      } else if (!strcmp(s, "newer")) {
        todo_store_argument();
      } else if (!strcmp(s, "exec") || !strcmp("ok", s)) {
        print++;
        if (check) error_exit("implement exec/ok");
      } else goto error;

      // This test can go at the end because we do a syntax checking
      // pass first. Putting it here gets the error message (-unknown
      // vs -known noarg) right.
      if (!*++ss) error_exit("'%s' needs 1 arg", --s);
    }

    // Apply pending "!" to result
    if (active && not) test = !test;
    not = 0;
  }

  // If there was no action, print
  if (!print && test && new) do_print(new, '\n');

  return recurse;

error:
  error_exit("bad arg '%s'", *ss);
}

void find_main(void)
{
  int i, len;
  char **ss = toys.optargs;

  // Distinguish paths from filters
  for (len = 0; toys.optargs[len]; len++)
    if (strchr("-!(", *toys.optargs[len])) break;
  TT.filter = toys.optargs+len;

  // use "." if no paths
  if (!*ss) {
    ss = (char *[]){"."};
    len = 1;
  }

  // first pass argument parsing, verify args match up, handle "evaluate once"
  TT.now = time(0);
  do_find(0);

  // Loop through paths
  for (i = 0; i < len; i++) {
    struct dirtree *new;

    new = dirtree_add_node(0, ss[i], toys.optflags&(FLAG_H|FLAG_L));
    if (new) dirtree_handle_callback(new, do_find);
  }
}
