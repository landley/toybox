/* vi: set ts=4 :*/
/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * Licensed under GPL version 2, see file LICENSE in this tarball for details.
 */

#include <stdio.h>
#include <strings.h>

/*
name
main()
struct
usage (short long example info)
path (/usr/sbin)
*/

int toybox_main(void)
{
	printf("toybox\n");
	return 0;
}

int toysh_main(void)
{
	printf("toysh\n");
}

// The monster fun applet list.

struct toy_list {
	char *name;
	int (*toy_main)(void);
} toy_list[] = {
	{"toybox", toybox_main},
	{"toysh", toysh_main}
};

// Global context for this applet.

struct toy_context {
	struct toy_list *which;
	int argc;
	char **argv;
	char buf[4096];
//	toy_union toydata;
} toys;

struct toy_list *find_toy_by_name(char *name)
{
	int top, bottom, middle;

	// If the name starts with "toybox", accept that as a match.  Otherwise
	// skip the first entry, which is out of order.

	if (!strncmp(name,"toybox",6)) return toy_list;
	bottom=1;

	// Binary search to find this applet.

	top=(sizeof(toy_list)/sizeof(struct toy_list))-1;
	for(;;) {
		int result;
		
		middle=(top+bottom)/2;
		if(middle<bottom || middle>top) return NULL;
		result = strcmp(name,toy_list[middle].name);
		if(!result) return toy_list+middle;
		if(result<0) top=--middle;
		else bottom=++middle;
	}
}

int main(int argc, char *argv[])
{
	char *name;

	// Record command line arguments.
	toys.argc = argc;
	toys.argv = argv;

	// Figure out which applet got called.
	name = rindex(argv[0],'/');
	if (!name) name = argv[0];
	else name++;
	toys.which = find_toy_by_name(name);

	if (!toys.which) {
		dprintf(2,"No behavior for %s\n",name);
		return 1;
	}
	return toys.which->toy_main();
}
