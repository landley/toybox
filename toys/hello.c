/* vi: set sw=4 ts=4:
 *
 * hello.c - A hello world program.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/

config HELLO
	bool "hello"
	default y
	help
	  A hello world program.  You don't need this.

	  Mostly used as an example/skeleton file for adding new commands,
	  occasionally nice to test kernel booting via "init=/bin/hello".
*/

#include "toys.h"

void hello_main(void)
{
	printf("Hello world\n");
}
