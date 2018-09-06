/* test.c - evaluate expression
 *
 * Copyright 2013 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/test.html

USE_TEST(NEWTOY(test, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config TEST
  bool "test"
  default n
  help
    usage: test [-bcdefghLPrSsuwx PATH] [-nz STRING] [-t FD] [X ?? Y]

    Return true or false by performing tests. (With no arguments return false.)

    --- Tests with a single argument (after the option):
    PATH is/has:
      -b  block device   -f  regular file   -p  fifo           -u  setuid bit
      -c  char device    -g  setgid         -r  read bit       -w  write bit
      -d  directory      -h  symlink        -S  socket         -x  execute bit
      -e  exists         -L  symlink        -s  nonzero size
    STRING is:
      -n  nonzero size   -z  zero size      (STRING by itself implies -n)
    FD (integer file descriptor) is:
      -t  a TTY

    --- Tests with one argument on each side of an operator:
    Two strings:
      =  are identical   !=  differ
    Two integers:
      -eq  equal         -gt  first > second    -lt  first < second
      -ne  not equal     -ge  first >= second   -le  first <= second

    --- Modify or combine tests:
      ! EXPR     not (swap true/false)   EXPR -a EXPR    and (are both true)
      ( EXPR )   evaluate this first     EXPR -o EXPR    or (is either true)
*/

#include "toys.h"

int get_stat(struct stat *st, int *link, char* s)
{
  if (lstat(s, st) == -1) return 0;
  *link = S_ISLNK(st->st_mode);
  if (*link && (stat(s, st) == -1)) return 0;
  return 1;
}

// basic expression without !, -o, -a, (
int test_basic(int optb, int opte)
{
  int id, val;
  char *s, *err_fmt = "Bad flag '%s'", *err_int = "Bad integer '%s'";

  if (optb == opte) val = 0;
  else if (optb + 1 == opte) val = !!*toys.optargs[optb];
  else if (optb + 2 == opte && toys.optargs[optb][0] == '-') {
    char c = toys.optargs[optb][1];
    struct stat st;
    int link;

    if (!c || toys.optargs[optb][2]) error_exit(err_fmt, toys.optargs[optb]);
    s = toys.optargs[optb + 1];
    if (c == 'b') val = get_stat(&st, &link, s) && S_ISBLK(st.st_mode);
    else if (c == 'c') val = get_stat(&st, &link, s) && S_ISCHR(st.st_mode);
    else if (c == 'd') val = get_stat(&st, &link, s) && S_ISDIR(st.st_mode);
    else if (c == 'e') val = get_stat(&st, &link, s);
    else if (c == 'f') val = get_stat(&st, &link, s) && S_ISREG(st.st_mode);
    else if (c == 'g') val = get_stat(&st, &link, s) && (st.st_mode & S_ISGID);
    else if (c == 'h' || c == 'L') val = get_stat(&st, &link, s) && link;
    else if (c == 'p') val = get_stat(&st, &link, s) && S_ISFIFO(st.st_mode);
    else if (c == 'S') val = get_stat(&st, &link, s) && S_ISSOCK(st.st_mode);
    else if (c == 's') val = get_stat(&st, &link, s) && st.st_size != 0;
    else if (c == 'u') val = get_stat(&st, &link, s) && (st.st_mode & S_ISUID);
    else if (c == 'r') val = access(s, R_OK) != -1;
    else if (c == 'w') val = access(s, W_OK) != -1;
    else if (c == 'x') val = access(s, X_OK) != -1;
    else if (c == 'z') val = !*s;
    else if (c == 'n') val = !!*s;
    else if (c == 't') {
      struct termios termios;
      val = tcgetattr(atoi(s), &termios) != -1;
    }
    else error_exit(err_fmt, toys.optargs[optb]);
  }
  else if (optb + 3 == opte) {
    if (*toys.optargs[optb + 1] == '-') {
      char *end_a, *end_b;
      long a, b;
      int errno_a, errno_b;

      errno = 0;
      a = strtol(toys.optargs[optb], &end_a, 10);
      errno_a = errno;
      b = strtol(toys.optargs[optb + 2], &end_b, 10);
      errno_b = errno;
      s = toys.optargs[optb + 1] + 1;
      if (!strcmp("eq", s)) val = a == b;
      else if (!strcmp("ne", s)) val = a != b;
      else if (!strcmp("gt", s)) val = a > b;
      else if (!strcmp("ge", s)) val = a >= b;
      else if (!strcmp("lt", s)) val = a < b;
      else if (!strcmp("le", s)) val = a <= b;
      else error_exit(err_fmt, toys.optargs[optb + 1]);
      if (!*toys.optargs[optb] || *end_a || errno_a)
        error_exit(err_int, toys.optargs[optb]);
      if (!*toys.optargs[optb + 2] || *end_b || errno_b)
        error_exit(err_int, toys.optargs[optb + 2]);
    }
    else {
      int result = strcmp(toys.optargs[optb], toys.optargs[optb + 2]);

      s = toys.optargs[optb + 1];
      if (!strcmp("=", s)) val = !result;
      else if (!strcmp("!=", s)) val = !!result;
      else error_exit(err_fmt, toys.optargs[optb + 1]);
    }
  }
  else error_exit("Syntax error");

  return val;
}

int test_sub(int optb, int opte)
{
  int not, and = 1, or = 0, i, expr;
  char *err_syntax = "Syntax error";

  for (;;) {
    not = 0;
    while (optb < opte && !strcmp("!", toys.optargs[optb])) {
      not = !not;
      optb++;
    }
    if (optb < opte && !strcmp("(", toys.optargs[optb])) {
      int par = 1;

      for (i = optb + 1; par && i < opte; i++) {
        if (!strcmp(")", toys.optargs[i])) par--;
        else if (!strcmp("(", toys.optargs[i])) par++;
      }
      if (par) error_exit("Missing ')'");
      expr = not ^ test_sub(optb + 1, i - 1);
      optb = i;
    }
    else {
      for (i = 0; i < 4; ++i) {
        if (optb + i == opte || !strcmp("-a", toys.optargs[optb + i])
            || !strcmp("-o", toys.optargs[optb + i])) break;
      }
      if (i == 4) error_exit(err_syntax);
      expr = not ^ test_basic(optb, optb + i);
      optb += i;
    }

    if (optb == opte) {
      return or || (and && expr);
    }
    else if (!strcmp("-o", toys.optargs[optb])) {
      or = or || (and && expr);
      and = 1;
      optb++;
    }
    else if (!strcmp("-a", toys.optargs[optb])) {
      and = and && expr;
      optb++;
    }
    else error_exit(err_syntax);
  }
}

int test_few_args(int optb, int opte)
{
  if (optb == opte) return 0;
  else if (optb + 1 == opte) return !!*toys.optargs[optb];
  else if (optb + 2 == opte) {
    if (!strcmp("!", toys.optargs[optb])) return !*toys.optargs[optb + 1];
    else if (toys.optargs[optb][0] == '-' &&
             stridx("bcdefghLpSsurwxznt", toys.optargs[optb][1]) != -1 &&
             !toys.optargs[optb][2]) return test_basic(optb, opte);
  }
  else if (optb + 3 == opte) {
    if (!strcmp("-eq", toys.optargs[optb + 1]) ||
        !strcmp("-ne", toys.optargs[optb + 1]) ||
        !strcmp("-gt", toys.optargs[optb + 1]) ||
        !strcmp("-ge", toys.optargs[optb + 1]) ||
        !strcmp("-lt", toys.optargs[optb + 1]) ||
        !strcmp("-le", toys.optargs[optb + 1]) ||
        !strcmp("=", toys.optargs[optb + 1]) ||
        !strcmp("!=", toys.optargs[optb + 1])) return test_basic(optb, opte);
    else if (!strcmp("!", toys.optargs[optb]))
      return !test_few_args(optb + 1, opte);
    else if (!strcmp("(", toys.optargs[optb]) &&
             !strcmp(")", toys.optargs[optb + 2]))
      return !!*toys.optargs[optb + 1];
  }
  else {
    if (!strcmp("!", toys.optargs[optb])) return !test_few_args(optb + 1, opte);
    else if (!strcmp("(", toys.optargs[optb]) &&
             !strcmp(")", toys.optargs[optb + 3]))
      return test_few_args(optb + 1, opte - 1);
  }
  return test_sub(optb, opte);
}

void test_main(void)
{
  int optc = toys.optc;

  toys.exitval = 2;
  if (!strcmp("[", toys.which->name))
    if (!optc || strcmp("]", toys.optargs[--optc])) error_exit("Missing ']'");
  if (optc <= 4) toys.exitval = !test_few_args(0, optc);
  else toys.exitval = !test_sub(0, optc);
  return;
}
