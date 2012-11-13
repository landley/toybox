/* false.c - Return nonzero.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/false.html

USE_FALSE(NEWTOY(false, NULL, TOYFLAG_BIN))

config FALSE
  bool "false"
  default y
  help
    Return nonzero.
*/

#include "toys.h"

void false_main(void)
{
  toys.exitval = 1;
}
