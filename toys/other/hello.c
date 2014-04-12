/* hello.c - A hello world program. (Template for new commands.)
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cmdbehav.html

// Accept many different kinds of command line argument:

USE_HELLO(NEWTOY(hello, "(walrus)(blubber):;(also):e@d*c#b:a", TOYFLAG_USR|TOYFLAG_BIN))
USE_HELLO_ALIAS(NEWTOY(hello_alias, "b:dq", TOYFLAG_USR|TOYFLAG_BIN))

config HELLO
  bool "hello"
  default n
  help
    usage: hello [-a] [-b STRING] [-c NUMBER] [-d LIST] [-e COUNT] [...]

    A hello world program.  You don't need this.

    Mostly used as an example/skeleton file for adding new commands,
    occasionally nice to test kernel booting via "init=/bin/hello".

config HELLO_ALIAS
  bool "hello_alias"
  default n
  depends on HELLO
  help
    usage: hello_alias [-dq] [-b NUMBER]

    Example of a second command with different arguments in the same source
    file as the first. Allows shared infrastructure not added to lib.
*/

#define FOR_hello
#include "toys.h"

// Hello doesn't use these globals, they're here for example/skeleton purposes.

GLOBALS(
  union {
    struct {
      char *b_string;
      long c_number;
      struct arg_list *d_list;
      long e_count;
      char *also_string;
      char *blubber_string;
    } h;
    struct {
      long b_number;
    } a;
  };

  int more_globals;
)

// Parse many different kinds of command line argument:

void hello_main(void)
{
  char **optargs;

  printf("Hello world\n");

  if (toys.optflags) printf("flags=%x\n", toys.optflags);
  if (toys.optflags & FLAG_a) printf("Saw a\n");
  if (toys.optflags & FLAG_b) printf("b=%s\n", TT.h.b_string);
  if (toys.optflags & FLAG_c) printf("c=%ld\n", TT.h.c_number);
  while (TT.h.d_list) {
    printf("d=%s\n", TT.h.d_list->arg);
    TT.h.d_list = TT.h.d_list->next;
  }
  if (TT.h.e_count) printf("e was seen %ld times\n", TT.h.e_count);
  for (optargs = toys.optargs; *optargs; optargs++)
    printf("optarg=%s\n", *optargs);
  if (toys.optflags & FLAG_walrus) printf("Saw --walrus\n");
  if (TT.h.blubber_string) printf("--blubber=%s\n", TT.h.blubber_string);
}

#define CLEANUP_hello
#define FOR_hello_alias
#include "generated/flags.h"

void hello_alias_main(void)
{
  printf("hello world %x\n", toys.optflags);
  if (toys.optflags & FLAG_b) printf("b=%ld", TT.a.b_number);
}
