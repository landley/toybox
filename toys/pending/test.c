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
      =  are identical	 !=  differ
    Two integers:
      -eq  equal         -gt  first > second    -lt  first < second
      -ne  not equal     -ge  first >= second   -le  first <= second

    --- Modify or combine tests:
      ! EXPR     not (swap true/false)   EXPR -a EXPR    and (are both true)
      ( EXPR )   evaluate this first     EXPR -o EXPR    or (is either true)
*/

#include "toys.h"

void test_main(void)
{
  int id, not;
  char *s, *err_fmt = "Bad flag '%s'";

  toys.exitval = 2;
  if (!strcmp("[", toys.which->name))
    if (!strcmp("]", toys.optargs[--toys.optc])) error_exit("Missing ']'");
  if (!strcmp("!", toys.optargs[0])) {
    not = 1;
    toys.optargs++;
    toys.optc--;
  }
  if (!toys.optc) toys.exitval = 0;
  else if (toys.optargs[0][0] == '-') {
    id = stridx("bcdefghLpSsurwxznt", toys.optargs[0][1]);
    if (id == -1 || toys.optargs[0][2]) error_exit(err_fmt, toys.optargs[0]);
    if (id < 12) {
      struct stat st;
      int nolink;

      toys.exitval = 1;
      if (lstat(toys.optargs[1], &st) == -1) return;
      nolink = !S_ISLNK(st.st_mode);
      if (!nolink && (stat(toys.optargs[1], &st) == -1)) return;

      if (id == 0) toys.exitval = !S_ISBLK(st.st_mode); // b
      else if (id == 1) toys.exitval = !S_ISCHR(st.st_mode); // c
      else if (id == 2) toys.exitval = !S_ISDIR(st.st_mode); // d
      else if (id == 3) toys.exitval = 0; // e
      else if (id == 4) toys.exitval = !S_ISREG(st.st_mode); // f
      else if (id == 5) toys.exitval = !(st.st_mode & S_ISGID); // g
      else if ((id == 6) || (id == 7)) toys.exitval = nolink; // hL
      else if (id == 8) toys.exitval = !S_ISFIFO(st.st_mode); // p
      else if (id == 9) toys.exitval = !S_ISSOCK(st.st_mode); // S
      else if (id == 10) toys.exitval = st.st_size == 0; // s
      else toys.exitval = !(st.st_mode & S_ISUID); // u
    }
    else if (id < 15) // rwx
      toys.exitval = access(toys.optargs[1], 1 << (id - 12)) == -1;
    else if (id < 17) // zn
      toys.exitval = toys.optargs[1] && !*toys.optargs[1] ^ (id - 15);
    else { // t
      struct termios termios;
      toys.exitval = tcgetattr(atoi(toys.optargs[1]), &termios) == -1;
    }
  }
  else if (toys.optc == 1) toys.exitval = *toys.optargs[0] == 0;
  else if (toys.optc == 3) {
    if (*toys.optargs[1] == '-') {
      long a = atol(toys.optargs[0]), b = atol(toys.optargs[2]);
      
      s = toys.optargs[1] + 1;
      if (!strcmp("eq", s)) toys.exitval = a != b;
      else if (!strcmp("ne", s)) toys.exitval = a == b;
      else if (!strcmp("gt", s)) toys.exitval = a < b;
      else if (!strcmp("ge", s)) toys.exitval = a <= b;
      else if (!strcmp("lt", s)) toys.exitval = a > b;
      else if (!strcmp("le", s)) toys.exitval = a >= b;
      else error_exit(err_fmt, toys.optargs[1]);
    }
    else {
      int result = strcmp(toys.optargs[0], toys.optargs[2]);

      s = toys.optargs[1];
      if (!strcmp("=", s)) toys.exitval = !!result;
      else if (!strcmp("!=", s)) toys.exitval = !result;
      else error_exit(err_fmt, toys.optargs[1]);
    }
  }
  toys.exitval ^= not;
  return;
}
