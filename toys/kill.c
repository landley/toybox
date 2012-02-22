/* vi: set sw=4 ts=4:
 *
 * kill.c - a program to send signals to processes
 *
 * Copyright 2012 Daniel Walter <d.walter@0x90.at>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/kill.html

USE_KILL(NEWTOY(kill, NULL, TOYFLAG_BIN))

config KILL
	bool "kill"
	default y
	help
	  usage: kill [-l signal_number | -s signal_name | -signal_name | -signal_number] pid...

	  Send a signal to a process

*/

#include "toys.h"

#define TT this.kill

typedef struct {
	int signum;
	char *signame;
} signals_t;

const signals_t signals[] = {
	{ 1, "HUP"},
	{ 2, "INT"},
	{ 3, "QUIT"},
	{ 4, "ILL"},
	{ 5, "TRAP"},
	{ 6, "ABRT"},
	{ 7, "BUS"},
	{ 8, "FPE"},
	{ 9, "KILL"},
	{ 10, "USR1"},
	{ 11, "SEGV"},
	{ 12, "USR2"},
	{ 13, "PIPE"},
	{ 14, "ALRM"},
	{ 15, "TERM"},
	{ 16, "STKFLT"},
	{ 17, "CHLD"},
	{ 18, "CONT"},
	{ 19, "STOP"},
	{ 20, "TSTP"},
	{ 21, "TTIN"},
	{ 22, "TTOU"},
	{ 23, "URG"},
	{ 24, "XCPU"},
	{ 25, "XFSZ"},
	{ 26, "VTALRM"},
	{ 27, "PROF"},
	{ 28, "WINCH"},
	{ 29, "POLL"},
	{ 30, "PWR"},
	{ 31, "SYS"},
	/* terminator */
	{ -1, NULL},
};

static char* signum_to_signame(int sig)
{
	int i = 0;
	for (;;) {
		if (signals[i].signum == sig)
			return signals[i].signame;

		if (signals[++i].signum == -1)
			break;
	}
	return NULL;
}

static int signame_to_signum(char *signame)
{
	int i=0;
	for (;;) {
		if (!strcmp(signals[i].signame, signame))
			return signals[i].signum;
		if (signals[++i].signum == -1)
			break;
	}
	return -1;
}

static int send_signal(int sig, pid_t pid)
{
	if (kill(pid, sig) < 0) {
		perror("kill");
		return -1;
	}
	return 0;
}

static void list_all_signals()
{
	int i = 0;
	for (;;) {
		printf("%s ", signals[i++].signame);
		if (i % 16 == 0)
			printf("\n");
		if (signals[i].signum == -1)
			break;
	}
	printf("\n");
}

static int list_signal(int signum)
{
	char *signam = signum_to_signame(signum);
	if (signam) {
		printf("%s\n", signam);
		return 0;
	} else {
		printf("Unknown signal %d\n", signum);
		return -1;
	}
}

static int list_signal_by_name(char *signame)
{
	int signum = signame_to_signum(signame);
	if (signum > 0) {
		printf("%d\n", signum);
		return 0;
	} else {
		printf("Unknown signal %s\n", signame);
		return -1;
	}
} 

void kill_main(void)
{
	int signum = 0;
	int have_signal = 0;
	char *signame, *tmp;
	pid_t pid;
	while (*toys.optargs) {
		char *arg = *(toys.optargs++);
		if (arg[0] == '-' && !have_signal) {
			arg++;
			switch(arg[0]) {
			case 'l':
				if (!*toys.optargs)
					list_all_signals();
				else {
					signum = strtol(*(toys.optargs), &signame, 10);
					if (signame == *(toys.optargs))
						list_signal_by_name(signame);
					else
						list_signal(signum);
				}
				return;
			case 's':
				arg = *(toys.optargs++);
			default:
				signum = strtol(arg, &signame, 10);
				if (signame == arg) {
					signum = signame_to_signum(signame);
					if (signum < 0) {
						toys.exitval = EXIT_FAILURE;
						return;
					}
				}
				have_signal = 1;
			}
		} else {
			/* pids */
			pid = strtol(arg, &tmp, 10);
			if (tmp == arg) {
				toys.exitval = EXIT_FAILURE;
				return;
			}
			if (send_signal(signum, pid) < 0) {
				toys.exitval = EXIT_FAILURE;
				return;
			}
				
		}
	}
}
