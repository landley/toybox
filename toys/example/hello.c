/* hello.c - A hello world program. (Simple template for new commands.)
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cmdbehav.html

USE_HELLO(NEWTOY(hello, 0, TOYFLAG_USR|TOYFLAG_BIN))

config HELLO
  bool "hello"
  default n
  help
    usage: hello [-s]

    A hello world program.  You don't need this.

    Mostly used as a simple template for adding new commands.
    Occasionally nice to smoketest kernel booting via "init=/usr/bin/hello".
*/

#define FOR_hello
#include "toys.h"

GLOBALS(
  int unused;
)

void hello_main(void)
{
  xprintf("Hello world\n");

  // Avoid kernel panic if run as init.
  if (getpid() == 1) wait(&TT.unused);
}
