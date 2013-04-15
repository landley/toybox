/* help.c - Show help for toybox commands
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Often a shell builtin.

USE_HELP(NEWTOY(help, "<1", TOYFLAG_BIN))

config HELP
  bool "help"
  default y
  depends on TOYBOX_HELP
  help
    usage: help [command]

    Show usage information for toybox commands.
    Run "toybox" with no arguments for a list of available commands.
*/


#include "toys.h"

void help_main(void)
{
  struct toy_list *t = toy_find(*toys.optargs);

  if (!t) error_exit("Unknown command '%s'", *toys.optargs);
  toys.which = t;
  show_help();
}
