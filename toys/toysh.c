/* vi: set sw=4 ts=4:
 * 
 * toysh - toybox shell
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * The spec for this is at:
 * http://www.opengroup.org/onlinepubs/009695399/utilities/xcu_chap02.html
 *
 * Although things like the bash man page are good to read too.
 */

// Handle embedded NUL bytes in the command line.

#include "toys.h"

static int handle(char *command)
{
	int argc = 0, status;
	char *argv[10], *start = command;
	pid_t pid;
	struct toy_list *tl;

	// Parse command into argv[]
	for (;;) {
		char *end;

		// Skip leading whitespace and detect EOL.
		while(isspace(*start)) start++;
		if (!*start || *start=='#') break;

		// Grab next word.  (Add dequote and envvar logic here)
		end = start;
		while (*end && !isspace(*end)) end++;
		argv[argc++] = xstrndup(start, end-start);
		start=end;
	}
	argv[argc]=0;

	if (!argc) return 0;

	tl = toy_find(argv[0]);
	// This is a bit awkward, next design cycle should clean it up.
	// Should vfork(), move to tryspawn()?
	pid = 0;
	if (tl && (tl->flags & TOYFLAG_NOFORK))
		status = tl->toy_main();
	else {
		pid=fork();
		if(!pid) {
			toy_exec(argv);
			xexec(argv);
		} else waitpid(pid, &status, 0);
	}
	while(argc) free(argv[--argc]);

	return 0;
}

int cd_main(void)
{
	char *dest = toys.argc>1 ? toys.argv[1] : getenv("HOME");
	if (chdir(dest)) error_exit("chdir %s",dest);
	return 0;
}

int exit_main(void)
{	
	exit(toys.argc>1 ? atoi(toys.argv[1]) : 0);
}

int toysh_main(void)
{
	char *command=NULL;
	FILE *f;

	// TODO get_optflags(argv, "c:", &command);

	f = toys.argv[1] ? xfopen(toys.argv[1], "r") : NULL;
	if (command) handle(command);
	else {
		unsigned cmdlen=0;
		for (;;) {
			if (!f) putchar('$');
			if (1 > getline(&command, &cmdlen, f ? : stdin)) break;
			handle(command);
		}
		if (CFG_TOYS_FREE) free(command);
	}
		
	return 1;
}
