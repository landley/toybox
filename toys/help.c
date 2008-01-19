/* vi: set sw=4 ts=4:
 *
 * help.c - Show help for toybox
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Not in SUSv3, but exists as a bash builtin.

USE_HELP(NEWTOY(help, "<1", TOYFLAG_BIN))

config HELP
	bool "help"
	default y
	help
	  usage: help [command]

	  Show usage information for toybox commands.

config HELP_LONG
	bool "Verbose help text"
	default y
	depends on HELP
	help
	  Show more than one line of help information per command.
*/
 

#include "toys.h"
#include "generated/help.h"

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name,opt,flags) help_##name "\0"
#define OLDTOY(name,oldname,opts,flags) "\xff" #oldname "\0"
static char *help_data =
#include "generated/newtoys.h"
;

void help_main(void)
{
	struct toy_list *t = toy_find(*toys.optargs);
	int i = t-toy_list;
	char *s = help_data;

	if (!t) error_exit("Unknown command '%s'", *toys.optargs);
	for (;;) {
		while (i--) s += strlen(s) + 1;
		if (*s != 255) break;
		i = toy_find(++s)-toy_list;
		s = help_data;
	}

	fprintf(toys.exithelp ? stderr : stdout, "%s", s);
}
