/* vi: set ts=4 :*/
/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// Populate toy_list[].

struct toy_list toy_list[] = {
#define FROM_MAIN
#include "toys/toylist.h"
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

void toy_init(struct toy_list *which, char *argv[])
{
	// Free old toys contents here?

	toys.which = which;
	toys.argv = argv;
	toys.exitval = 1;
}

// Run a toy.
void toy_exec(char *argv[])
{
	struct toy_list *which;
	
	which = toy_find(argv[0]);
	if (!which) return;

	toy_init(which, argv);
	
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
