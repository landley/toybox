/* skeleton.c - Example program to act as template for new commands.
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cmdbehav.html

// Accept many different kinds of command line argument (see top of lib/args.c)
// Demonstrate two commands in the same file (see www/documentation.html)

USE_SKELETON(NEWTOY(skeleton, "(walrus)(blubber):;(also):e@d*c#b:a", TOYFLAG_USR|TOYFLAG_BIN))
USE_SKELETON_ALIAS(NEWTOY(skeleton_alias, "b#dq", TOYFLAG_USR|TOYFLAG_BIN))

config SKELETON
  bool "skeleton"
  default n
  help
    usage: skeleton [-a] [-b STRING] [-c NUMBER] [-d LIST] [-e COUNT] [...]

    Template for new commands. You don't need this.

    When creating a new command, copy this file and delete the parts you
    don't need. Be sure to replace all instances of "skeleton" (upper and lower
    case) with your new command name.

    For simple commands, "hello.c" is probably a better starting point.

config SKELETON_ALIAS
  bool "skeleton_alias"
  default n
  help
    usage: skeleton_alias [-dq] [-b NUMBER]

    Example of a second command with different arguments in the same source
    file as the first. This allows shared infrastructure not added to lib/.
*/

#define FOR_skeleton
#include "toys.h"

// The union lets lib/args.c store arguments for either command.
// It's customary to put a space between argument variables and other globals.
GLOBALS(
  union {
    struct {
      char *b_string;
      long c_number;
      struct arg_list *d_list;
      long e_count;
      char *also_string;
      char *blubber_string;
    } s;
    struct {
      long b_number;
    } a;
  };

  int more_globals;
)

// Don't blindly build allyesconfig. The maximum _sane_ config is defconfig.
#warning skeleton.c is just an example, not something to deploy.

// Parse many different kinds of command line argument:
void skeleton_main(void)
{
  char **optargs;

  printf("Ran %s\n", toys.which->name);

  // Command line options parsing is done for you by lib/args.c called
  // from main.c using the optstring in the NEWTOY macros. Display results.
  if (toys.optflags) printf("flags=%x\n", toys.optflags);
  if (toys.optflags & FLAG_a) printf("Saw a\n");
  if (toys.optflags & FLAG_b) printf("b=%s\n", TT.s.b_string);
  if (toys.optflags & FLAG_c) printf("c=%ld\n", TT.s.c_number);
  while (TT.s.d_list) {
    printf("d=%s\n", TT.s.d_list->arg);
    TT.s.d_list = TT.s.d_list->next;
  }
  if (TT.s.e_count) printf("e was seen %ld times\n", TT.s.e_count);
  for (optargs = toys.optargs; *optargs; optargs++)
    printf("optarg=%s\n", *optargs);
  if (toys.optflags & FLAG_walrus) printf("Saw --walrus\n");
  if (TT.s.blubber_string) printf("--blubber=%s\n", TT.s.blubber_string);

  printf("Other globals should start zeroed: %d\n", TT.more_globals);
}

// Switch gears from skeleton to skeleton_alias (swap FLAG macros).
#define CLEANUP_skeleton
#define FOR_skeleton_alias
#include "generated/flags.h"

void skeleton_alias_main(void)
{
  printf("Ran %s\n", toys.which->name);
  printf("flags=%x\n", toys.optflags);

  // Note, this FLAG_b is a different bit position than the other FLAG_b,
  // and fills out a different variable of a different type.
  if (toys.optflags & FLAG_b) printf("b=%ld", TT.a.b_number);
}
