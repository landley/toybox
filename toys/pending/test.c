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
  return;
}
