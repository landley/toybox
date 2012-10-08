/* vi: set sw=4 ts=4:
 *
 * kill.c - a program to send signals to processes
 *
 * Copyright 2012 Daniel Walter <d.walter@0x90.at>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/kill.html

USE_KILL(NEWTOY(kill, "?s: l", TOYFLAG_BIN))

config KILL
	bool "kill"
	default y
	help
	  usage: kill [-l [SIGNAL] | -s SIGNAL | -SIGNAL] pid...

	  Send a signal to a process

*/

#define FOR_kill
#include "toys.h"

GLOBALS(
	char *signame;
)

void kill_main(void)
{
	int signum;
	char *tmp, **args = toys.optargs;
	pid_t pid;

	// list signal(s)
	if (toys.optflags & FLAG_l) {
		if (*args) {
			int signum = sig_to_num(*args);
			char *s = NULL;

			if (signum>=0) s = num_to_sig(signum&127);
			puts(s ? s : "UNKNOWN");
		} else sig_to_num(NULL);
		return;
	}

	// signal must come before pids, so "kill -9 -1" isn't confusing.

	if (!TT.signame && *args && **args=='-') TT.signame=*(args++)+1;
	if (TT.signame) {
		char *arg;
		int i = strtol(TT.signame, &arg, 10);
		if (!*arg) arg = num_to_sig(i);
		else arg = TT.signame;

		if (!arg || -1 == (signum = sig_to_num(arg)))
			error_exit("Unknown signal '%s'", arg);
	} else signum = SIGTERM;

	if (!*args) {
		toys.exithelp++;
		error_exit("missing argument");
	}

	while (*args) {
		char *arg = *(args++);

		pid = strtol(arg, &tmp, 10);
		if (*tmp || kill(pid, signum) < 0) {
			error_msg("unknown pid '%s'", arg);
			toys.exitval = EXIT_FAILURE;
		}
	}
}
