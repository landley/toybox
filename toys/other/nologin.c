/* nologin.c - False with a message.
 *
 * Copyright 2025 Rob Landley <rob@landley.net>
 *
 * No standard.

USE_NOLOGIN(NEWTOY(nologin, 0, TOYFLAG_BIN|TOYFLAG_NOHELP))

config NOLOGIN
  bool "nologin"
  default y
  help
    usage: nologin

    Print /etc/nologin.txt and return failure.
*/

#include "toys.h"

void nologin_main(void)
{
  toys.exitval = 1;
  puts(readfile("/etc/nologin.txt", 0, 0) ? : toys.which->name);
}
