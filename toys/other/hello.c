/* hello.c - A hello world program. (Template for new commands.)
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cmdbehav.html

// Accept many different kinds of command line argument:

USE_HELLO(NEWTOY(hello, "(walrus)(blubber):;(also):e@d*c#b:a", TOYFLAG_USR|TOYFLAG_BIN))

config HELLO
  bool "hello"
  default n
  help
    usage: hello [-a] [-b string] [-c number] [-d list] [-e count] [...]

    A hello world program.  You don't need this.

    Mostly used as an example/skeleton file for adding new commands,
    occasionally nice to test kernel booting via "init=/bin/hello".
*/

#define FOR_hello
#include "toys.h"

// Hello doesn't use these globals, they're here for example/skeleton purposes.

GLOBALS(
  char *b_string;
  long c_number;
  struct arg_list *d_list;
  long e_count;
  char *also_string;
  char *blubber_string;

  int more_globals;
)

// Parse many different kinds of command line argument:

void hello_main(void)
{
  printf("Hello world\n");

  if (toys.optflags) printf("flags=%x\n", toys.optflags);
  if (toys.optflags & FLAG_a) printf("Saw a\n");
  if (toys.optflags & FLAG_b) printf("b=%s\n", TT.b_string);
  if (toys.optflags & FLAG_c) printf("c=%ld\n", TT.c_number);
  while (TT.d_list) {
    printf("d=%s\n", TT.d_list->arg);
    TT.d_list = TT.d_list->next;
  }
  if (TT.e_count) printf("e was seen %ld times\n", TT.e_count);
  while (*toys.optargs) printf("optarg=%s\n", *(toys.optargs++));
  if (toys.optflags & FLAG_walrus) printf("Saw --walrus\n");
  if (TT.blubber_string) printf("--blubber=%s\n", TT.blubber_string);
}
