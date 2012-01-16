/* vi: set sw=4 ts=4:
 *
 * hello.c - A hello world program.
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * Not in SUSv4.
 * See http://opengroup.org/onlinepubs/9699919799/utilities/

USE_HELLO(NEWTOY(hello, "e@d*c#b:a", TOYFLAG_USR|TOYFLAG_BIN))

config HELLO
	bool "hello"
	default n
	help
	  usage: hello [-a] [-b string] [-c number] [-d list] [-e count] [...]

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

#define FLAG_a	1
#define FLAG_b	2
#define FLAG_c	4
#define FLAG_d	8
#define FLAG_e	16

void hello_main(void)
{
	printf("Hello world\n");

	if (toys.optflags & FLAG_a) printf("Saw a\n");
	if (toys.optflags & FLAG_b) printf("b=%s\n", TT.b_string);
	if (toys.optflags & FLAG_c) printf("c=%ld\n", TT.c_number);
	while (TT.d_list) {
		printf("d=%s\n", TT.d_list->arg);
		TT.d_list = TT.d_list->next;
	}
	if (TT.e_count) printf("e was seen %ld times", TT.e_count);

	while (*toys.optargs) printf("optarg=%s\n", *(toys.optargs++));
}
