/* vi: set ts=4 :*/
/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// Populate toy_list[].

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name, opts, flags) {#name, name##_main, opts, flags},
#define OLDTOY(name, oldname, opts, flags) {#name, oldname##_main, opts, flags},

struct toy_list toy_list[] = {
#include "generated/newtoys.h"
};

#define TOY_LIST_LEN (sizeof(toy_list)/sizeof(struct toy_list))

// global context for this applet.

struct toy_context toys;
union global_union this;
char toybuf[4096];

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

// Figure out whether or not anything is using the option parsing logic,
// because the compiler can't figure out whether or not to optimize it away
// on its' own.

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name, opts, flags) opts ||
#define OLDTOY(name, oldname, opts, flags) opts ||
static const int NEED_OPTIONS =
#include "generated/newtoys.h"
0;  // Ends the opts || opts || opts...

void toy_init(struct toy_list *which, char *argv[])
{
	// Free old toys contents here?

	toys.which = which;
	toys.argv = argv;
	if (NEED_OPTIONS && which->options) get_optflags();
	else toys.optargs = argv+1;
	if (which->flags & TOYFLAG_UMASK) toys.old_umask = umask(0);
}

// Run a toy.
void toy_exec(char *argv[])
{
	struct toy_list *which;

	which = toy_find(argv[0]);
	if (!which) return;
	toy_init(which, argv);
	toys.which->toy_main();
	exit(toys.exitval);
}

void toybox_main(void)
{
	static char *toy_paths[]={"usr/","bin/","sbin/",0};
	int i, len = 0;

	if (toys.argv[1]) {
		if (toys.argv[1][0]!='-') {
			toy_exec(toys.argv+1);
			toys.which = toy_list;
			error_exit("Unknown command %s",toys.argv[1]);
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
}

int main(int argc, char *argv[])
{
	// Artificial scope to eat less stack for things we call
	{
		char *name;

		// Figure out which applet to call.
		name = rindex(argv[0], '/');
		if (!name) name=argv[0];
		else name++;
		argv[0] = name;
	}

	toys.argv = argv-1;
	toybox_main();
	return 0;
}
