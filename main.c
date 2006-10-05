/* vi: set ts=4 :*/
/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * Licensed under GPL version 2, see file LICENSE in this tarball for details.
 */

#include "toys.h"

// The monster fun applet list.

struct toy_list toy_list[] = {
	// This one is out of order on purpose.
	{"toybox", toybox_main, 0},
	// The rest of these are alphabetical, for binary search.
	{"cd", cd_main, TOYFLAG_NOFORK},
	{"df", df_main, TOYFLAG_USR|TOYFLAG_SBIN},
	{"exit", exit_main, TOYFLAG_NOFORK},
	{"toysh", toysh_main, TOYFLAG_BIN}
};

#define TOY_LIST_LEN (sizeof(toy_list)/sizeof(struct toy_list))

// global context for this applet.

struct toy_context toys;

struct toy_list *toy_find(char *name)
{
	int top, bottom, middle;

	// If the name starts with "toybox", accept that as a match.  Otherwise
	// skip the first entry, which is out of order.

	if (!strncmp(name,"toybox",6)) return toy_list;
	bottom = 1;

	// Binary search to find this applet.

	top = TOY_LIST_LEN-1;
	for (;;) {
		int result;
		
		middle = (top+bottom)/2;
		if (middle<bottom || middle>top) return NULL;
		result = strcmp(name,toy_list[middle].name);
		if (!result) return toy_list+middle;
		if (result<0) top=--middle;
		else bottom = ++middle;
	}
}

// Run a toy.
void toy_exec(char *argv[])
{
	struct toy_list *which;
	
	which = toy_find(argv[0]);
	if (!which) return;

	// Free old toys contents here?

	toys.which = which;
	toys.argv = argv;
	for (toys.argc = 0; argv[toys.argc]; toys.argc++);
	toys.exitval = 1;
	
	exit(toys.which->toy_main());
}

int toybox_main(void)
{
	static char *toy_paths[]={"usr/","bin/","sbin/",0};
	int i, len = 0;

	if (toys.argv[1]) {
		if (toys.argv[1][0]!='-') {
			toy_exec(toys.argv+1);
			error_exit("No behavior for %s\n",toys.argv[1]);
		}
	}

	// Output list of applets.
	for (i=1; i<TOY_LIST_LEN; i++) {
		int fl = toy_list[i].flags;
		if (fl & TOYMASK_LOCATION) {
			if (toys.argv[1]) {
				int j;
				for (j=0; toy_paths[j]; j++)
					if (fl & (1<<j)) len += printf("%s", toy_paths[j]);
			}
			len += printf("%s ",toy_list[i].name);
			if (len>65) {
				putchar('\n');
				len=0;
			}
		}
	}
	putchar('\n');
	return 0;
}

int main(int argc, char *argv[])
{
	char *name;

	// Figure out which applet to call.
	name = rindex(argv[0], '/');
	if (!name) name=argv[0];
	else name++;
	argv[0] = name;

	toys.argv = argv-1;
	return toybox_main();
}
