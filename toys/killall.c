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
	int matched;
	int signum;
)
#define TT this.killall

struct signame {
	int num;
	const char *name;
};

static struct signame signames[] = {
#ifdef SIGHUP
	{SIGHUP, "HUP"},
#endif
#ifdef SIGINT
	{SIGINT, "INT"},
#endif
#ifdef SIGQUIT
	{SIGQUIT, "QUIT"},
#endif
#ifdef SIGILL
	{SIGILL, "ILL"},
#endif
#ifdef SIGTRAP
	{SIGTRAP, "TRAP"},
#endif
#ifdef SIGTABRT
	{SIGABRT, "ABRT"},
#endif
#ifdef SIGTABRT
	{SIGIOT, "IOT"},
#endif
#ifdef SIGBUS
	{SIGBUS, "BUS"},
#endif
#ifdef SIGFPE
	{SIGFPE, "FPE"},
#endif
#ifdef SIGKILL
	{SIGKILL, "KILL"},
#endif
#ifdef SIGUSR1
	{SIGUSR1, "USR1"},
#endif
#ifdef SIGSEGV
	{SIGSEGV, "SEGV"},
#endif
#ifdef SIGUSR2
	{SIGUSR2, "USR2"},
#endif
#ifdef SIGPIPE
	{SIGPIPE, "PIPE"},
#endif
#ifdef SIGALRM
	{SIGALRM, "ALRM"},
#endif
#ifdef SIGTERM
	{SIGTERM, "TERM"},
#endif
#ifdef SIGSTKFLT
	{SIGSTKFLT, "STKFLT"},
#endif
#ifdef SIGCHLD
	{SIGCHLD, "CHLD"},
#endif
#ifdef SIGCONT
	{SIGCONT, "CONT"},
#endif
#ifdef SIGSTOP
	{SIGSTOP, "STOP"},
#endif
#ifdef SIGSTOP
	{SIGSTOP, "STOP"},
#endif
#ifdef SIGTSTP
	{SIGTSTP, "TSTP"},
#endif
#ifdef SIGTTIN
	{SIGTTIN, "TTIN"},
#endif
#ifdef SIGTTOU
	{SIGTTOU, "TTOU"},
#endif
#ifdef SIGURG
	{SIGURG, "URG"},
#endif
#ifdef SIGXCPU
	{SIGXCPU, "XCPU"},
#endif
#ifdef SIGXFSZ
	{SIGXFSZ, "XFSZ"},
#endif
#ifdef SIGVTALRM
	{SIGVTALRM, "VTALRM"},
#endif
#ifdef SIGVTALRM
	{SIGVTALRM, "VTALRM"},
#endif
#ifdef SIGPROF
	{SIGPROF, "PROF"},
#endif
#ifdef SIGWINCH
	{SIGWINCH, "WINCH"},
#endif
#ifdef SIGIO
	{SIGIO, "IO"},
#endif
#ifdef SIGPOLL
	{SIGPOLL, "POLL"},
#endif
#ifdef SIGPWR
	{SIGPWR, "PWR"},
#endif
#ifdef SIGSYS
	{SIGSYS, "SYS"},
#endif
#ifdef SIGUNUSED
	{SIGUNUSED, "UNUSED"},
#endif
	{0, NULL}
};

static int sig_to_num(const char *pidstr)
{
	int i, num;

	if (isdigit(pidstr[0])) {
		num = atoi(pidstr);

		return num;
	}

	for (i = 0; signames[i].num; i++) {
		if (strcmp(pidstr, signames[i].name) == 0) {
			return signames[i].num;
		}
	}

	return -1;
}

static void print_signals()
{
	int i;

	for (i = 0; signames[i].num; i++) {
		puts(signames[i].name);
	}
}

static void kill_process(pid_t pid)
{
	int ret;

	TT.matched = 1;
	ret = kill(pid, TT.signum);

	if (ret == -1 && !(toys.optflags & FLAG_q)) perror("kill");
}

void killall_main(void)
{
	char **names;

	TT.signum = SIGTERM;

	if (toys.optflags & FLAG_l) {
		print_signals();
		return;
	}

	if (!*toys.optargs) {
		toys.exithelp = 1;
		error_exit("Process name missing!");
	}

	names = toys.optargs;

	if (**names == '-') {
		TT.signum = sig_to_num((*names)+1);
		if (TT.signum <= 0) {
			if (toys.optflags & FLAG_q) error_exit("Invalid signal");
			exit(1);
		}
		names++;
	}

	if (!*names) {
		toys.exithelp = 1;
		error_exit("Process name missing!");
	}

	for_each_pid_with_name_in(names, kill_process);

	if (!TT.matched) {
		if (!(toys.optflags & FLAG_q)) fprintf(stderr, "No such process\n");
		exit(1);
	}
}
