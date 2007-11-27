/* vi: set sw=4 ts=4: */
/*
 * help.c - Show help for toybox
 */

#include "toys.h"
#include "toys/help.h"

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name,opt,flags) help_##name "\0"
#define OLDTOY(name,oldname,opts,flags) "\xff" #oldname "\0"
static char *help_data =
#include "toys/toylist.h"
;

int help_main(void)
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
	return 0;
}
