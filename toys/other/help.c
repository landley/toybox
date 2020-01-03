/* help.c - Show help for toybox commands
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Often a shell builtin.

USE_HELP(NEWTOY(help, ""USE_HELP_EXTRAS("ahu"), TOYFLAG_BIN|TOYFLAG_MAYFORK))

config HELP
  bool "help"
  default y
  depends on TOYBOX_HELP
  help
    usage: help [command]

    Show usage information for toybox commands.
    Run "toybox" with no arguments for a list of available commands.

config HELP_EXTRAS
  bool "help -ah"
  default y
  depends on TOYBOX
  depends on HELP
  help
    usage: help [-ah] [COMMAND]

    -a	All commands
    -u	Usage only
    -h	HTML output
*/

#define FOR_help
#include "toys.h"

static void do_help(struct toy_list *t)
{
  if (FLAG(h))
    xprintf("<a name=\"%s\"><h1>%s</h1><blockquote><pre>\n", t->name, t->name);

  toys.which = t;
  show_help(stdout, !FLAG(u));

  if (FLAG(h)) xprintf("</blockquote></pre>\n");
}

// The simple help is just toys.which = toy_find("name"); show_help(stdout);
// But iterating through html output and all commands is a big more 

void help_main(void)
{
  int i;

  // If called with no arguments as a builtin form the shell, show all builtins
  if (!*toys.optargs && toys.rebound) {
    for (i = 0; i < toys.toycount; i++) {
      if (!(toy_list[i].flags&(TOYFLAG_NOFORK|TOYFLAG_MAYFORK))) continue;
      toys.which = toy_list+i;
      show_help(stdout, 0);
    }
    return;
  }

  if (!FLAG(a)) {
    struct toy_list *t = toys.which;

    if (*toys.optargs && !(t = toy_find(*toys.optargs)))
      error_exit("Unknown command '%s'", *toys.optargs);
    do_help(t);
    return;
  }

  if (FLAG(h)) {
    xprintf("<html>\n<title>Toybox command list</title>\n<body>\n<p>\n");
    for (i=0; i < toys.toycount; i++)
      xprintf("<a href=\"#%s\">%s\n</a>\n", toy_list[i].name,
              toy_list[i].name);
    xprintf("</p>\n");
  }

  for (i = 0; i < toys.toycount; i++) {
    if (FLAG(h)) xprintf("<hr>\n<pre>\n");
    else if (!FLAG(u)) {
      memset(toybuf, '-', 78);
      memcpy(toybuf+3, toy_list[i].name, strlen(toy_list[i].name));
      printf("\n%s\n\n", toybuf);
    }
    do_help(toy_list+i);
    if (FLAG(h)) xprintf("</pre>\n");
  }

  if (FLAG(h)) xprintf("</html>");
}
