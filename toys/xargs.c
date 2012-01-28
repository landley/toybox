/* vi: set sw=4 ts=4:
 *
 * xargs.c - Run command with arguments taken from stdin.
 *
 * Copyright 2011 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/xargs.html

USE_XARGS(NEWTOY(xargs, "n#s#0", TOYFLAG_USR|TOYFLAG_BIN))

config XARGS
	bool "xargs"
	default y
	help
	  usage: xargs [-ptxr0] [-s NUM] [-n NUM] [-L NUM] [-E STR] COMMAND...

	  Run command line one or more times, appending arguments from stdin.

	  -s	Size in bytes per command line
	  -n	Max number of arguments per command
	  -0	Each argument is NULL terminated, no whitespace or quote processing
*/

#include "toys.h"

DEFINE_GLOBALS(
	long max_bytes;
	long max_entries;
	long L;
	char *E;
	char *I;

	long entries, bytes;
	char delim;
)

#define TT this.xargs

// According to man execv(5), the actual ARGS_MAX for linux is 128k (131072)
// meaning the theoretical maximum arguments (each one char) is 65536... but
// we can just use toybuf (1024 pointer on 32 bit, 512 on 64 bit).

#define ACTUAL_ARGS_MAX 131072

// If out==NULL count TT.bytes and TT.entries, stopping at max.
// Otherwise, fill out out[] 

// Returning NULL means need more data.
// Returning 1 means hit data limits, but consumed all data
// Returning char * means hit data limits, start of data left over

static char *handle_entries(char *data, char **entry)
{
	if (TT.delim) { 
		char *s = data;

		// Chop up whitespace delimited string into args
		while (*s) {
			char *save;

			while (isspace(*s)) {
				if (entry) *s = 0;
				s++;
			}

			if (TT.entries >= TT.max_entries && TT.max_entries)
				return *s ? (char *)1: s;

			if (!*s) break;
			save = s;

			for (;;) {
				if (++TT.bytes >= TT.max_bytes && TT.max_bytes) return save;
				if (!*s || isspace(*s)) break;
				s++;
			}
			if (entry) entry[TT.entries] = save;
			++TT.entries;
		}
	} else {
		if (entry) entry[TT.entries] = data;
		TT.bytes += strlen(data);
		if (TT.bytes >= TT.max_bytes || ++TT.entries >= TT.max_entries)
			return (char *)1;
	}

	return NULL;
}

void xargs_main(void)
{
	struct double_list *dlist = NULL;
	int entries, bytes, done = 0, status;
	char *data = NULL;

	if (!(toys.optflags&1)) TT.delim = '\n';

	// If no optargs, call echo.
	if (!toys.optc) {
		free(toys.optargs);
		*(toys.optargs=xzalloc(2*sizeof(char *)))="echo";
		toys.optc=1;
	}

	for (entries = 0, bytes = -1; entries < toys.optc; entries++, bytes++)
		bytes += strlen(toys.optargs[entries]);

	// Loop through exec chunks.
	while (!done) {
		TT.entries = 0;
		TT.bytes = bytes;
		char **out;

		// Loop reading input
		for (;;) {

			// Read line
			if (!data) {
				ssize_t l = 0;
				l = getdelim(&data, (size_t *)&l, TT.delim, stdin);

				if (l<0) {
					data = 0;
					done++;
					break;
				}
			}
			dlist_add(&dlist, data);

			// Count data used
			data = handle_entries(data, NULL);
			if (!data) continue;
			if (data == (char *)1) data = 0;
			else data = xstrdup(data);

			break;
		}

		// Accumulate cally thing

		if (data && !TT.entries) error_exit("argument too long");
		out = xzalloc((entries+TT.entries+1)*sizeof(char *));

		if (dlist) {
			struct double_list *dtemp;

			// Fill out command line to exec
			memcpy(out, toys.optargs, entries*sizeof(char *));
			TT.entries = 0;
			TT.bytes = bytes;
			dlist->prev->next = 0;
			for (dtemp = dlist; dtemp; dtemp = dtemp->next)
				handle_entries(dtemp->data, out+entries);
		}
		pid_t pid=fork();
		if (!pid) {
			xclose(0);
			open("/dev/null", O_RDONLY);
			xexec(out);
		}
		waitpid(pid, &status, 0);
		status = WEXITSTATUS(status);

		// Abritrary number of execs, can't just leak memory each time...
		while (dlist) {
			struct double_list *dtemp = dlist->next;

			free(dlist->data);
			free(dlist);
			dlist = dtemp;
		}
		free(out);
	}
}
