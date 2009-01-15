/* vi: set sw=4 ts=4:
 *
 * hello.c - A hello world program.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3.
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/

USE_HELLO(NEWTOY(hello, "e@d*c#b:a", TOYFLAG_USR|TOYFLAG_BIN))

config HELLO
	bool "hello"
	default n
	help
	  A hello world program.  You don't need this.

	  Mostly used as an example/skeleton file for adding new commands,
	  occasionally nice to test kernel booting via "init=/bin/hello".
*/

#include "toys.h"

// Hello doesn't use these globals, they're here for example/skeleton purposes.

DEFINE_GLOBALS(
	char *b_string;
	long c_number;
	struct arg_list *d_list;
	long e_count;

	int more_globals;
)

#define TT this.hello

void hello_main(void)
{
	printf("Hello world\n");
}
