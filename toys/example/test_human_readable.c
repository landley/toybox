/* test_human_readable.c - Expose lib/lib.c human_readable() for testing.
 *
 * Copyright 2015 Rob Landley <rob@landley.net>

USE_TEST_HUMAN_READABLE(NEWTOY(test_human_readable, "<1>1ibs", TOYFLAG_BIN))

config TEST_HUMAN_READABLE
  bool "test_human_readable"
  default n
  help
    usage: test_human_readable [-sbi] NUMBER
*/

#define FOR_test_human_readable
#include "toys.h"

void test_human_readable_main(void)
{
  char *c;

  human_readable(toybuf, strtoll(*toys.optargs, &c, 0), toys.optflags);
  printf("%s\n", toybuf);
}
