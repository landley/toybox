/* vi: set sw=4 ts=4:
 *
 * chvt.c - switch virtual terminals
 *
 * Copyright (C) 2008 David Anders <danders@amltd.com>
 *
 * Not in SUSv3.

USE_CHVT(NEWTOY(chvt, "<1", TOYFLAG_USR|TOYFLAG_SBIN))

config CHVT
	bool "chvt"
	default y
	help
	  usage: chvt N

	  Change to virtual terminal number N.  (This only works in text mode.)

	  Virtual terminals are the Linux VGA text mode displays, ordinarily
	  switched between via alt-F1, alt-F2, etc.  Use ctrl-alt-F1 to switch
	  from X to a virtual terminal, and alt-F6 (or F7, or F8) to get back.
*/

#include "toys.h"

/* Note: get_console_fb() will need to be moved into a seperate lib section */
int get_console_fd()
{
	int fd;
	char *consoles[]={"/dev/console", "/dev/vc/0", "/dev/tty", NULL}, **cc;

	cc = consoles;
	while (*cc) {
		fd = open(*cc++, O_RDWR);
		if (fd >= 0) return fd;
	}

	return -1;
}

void chvt_main(void)
{
	int vtnum, fd;

	vtnum=atoi(*toys.optargs);

	fd=get_console_fd();
	// These numbers are VT_ACTIVATE and VT_WAITACTIVE from linux/vt.h
	if (fd < 0 || ioctl(fd, 0x5606, vtnum) || ioctl(fd, 0x5607, vtnum))
		perror_exit(NULL);
}
