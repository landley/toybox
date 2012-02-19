/* vi: set sw=4 ts=4:
 *
 * killall.c - Send signal (default: TERM) to all processes with given names.
 *
 * Copyright 2012 Andreas Heck <aheck@gmx.de>
 *
 * Not in SUSv4.

USE_KILLALL(NEWTOY(killall, "<1?lq", TOYFLAG_USR|TOYFLAG_BIN))

config KILLALL
	bool "killall"
	default y
	help
	  usage: killall [-l] [-q] [-SIG] PROCESS_NAME...

	  Send a signal (default: TERM) to all processes with the given names.

		-l	print list of all available signals
		-q	don't print any warnings or error messages
*/

#include "toys.h"

#define FLAG_q	1
#define FLAG_l	2

DEFINE_GLOBALS(
	int signum;
)
#define TT this.killall

struct signame {
	int num;
	char *name;
};

// Signals required by POSIX 2008:
// http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/signal.h.html

#define SIGNIFY(x) {SIG##x, #x}

static struct signame signames[] = {
	SIGNIFY(ABRT), SIGNIFY(ALRM), SIGNIFY(BUS), SIGNIFY(CHLD), SIGNIFY(CONT),
	SIGNIFY(FPE), SIGNIFY(HUP), SIGNIFY(ILL), SIGNIFY(INT), SIGNIFY(KILL),
	SIGNIFY(PIPE), SIGNIFY(QUIT), SIGNIFY(SEGV), SIGNIFY(STOP), SIGNIFY(TERM),
	SIGNIFY(TSTP), SIGNIFY(TTIN), SIGNIFY(TTOU), SIGNIFY(USR1), SIGNIFY(USR2),
	SIGNIFY(SYS), SIGNIFY(TRAP), SIGNIFY(URG), SIGNIFY(VTALRM), SIGNIFY(XCPU),
	SIGNIFY(XFSZ)
};

// SIGNIFY(STKFLT), SIGNIFY(WINCH), SIGNIFY(IO), SIGNIFY(PWR)

// Convert name to signal number.  If name == NULL print names.
static int sig_to_num(char *pidstr)
{
	int i;

	if (pidstr) {
		if (isdigit(*pidstr)) return atol(pidstr);
		if (!strncasecmp(pidstr, "sig", 3)) pidstr+=3;
	}
	for (i = 0; i < sizeof(signames)/sizeof(struct signame); i++)
		if (!pidstr) xputs(signames[i].name);
		else if (!strcasecmp(pidstr, signames[i].name))
			return signames[i].num;

	return -1;
}

static void kill_process(pid_t pid)
{
	int ret;

	toys.exitval = 0;
	ret = kill(pid, TT.signum);

	if (ret == -1 && !(toys.optflags & FLAG_q)) perror("kill");
}

void killall_main(void)
{
	char **names;

	if (toys.optflags & FLAG_l) {
		sig_to_num(NULL);
		return;
	}

	TT.signum = SIGTERM;
	toys.exitval++;

	if (!*toys.optargs) {
		toys.exithelp = 1;
		error_exit("Process name missing!");
	}

	names = toys.optargs;

	if (**names == '-') {
		if (0 > (TT.signum = sig_to_num((*names)+1))) {
			if (toys.optflags & FLAG_q) exit(1);
			error_exit("Invalid signal");
		}
		names++;

		if (!*names) {
			toys.exithelp++;
			error_exit("Process name missing!");
		}
	}

	for_each_pid_with_name_in(names, kill_process);

	if (toys.exitval && !(toys.optflags & FLAG_q))
		error_exit("No such process");
}
