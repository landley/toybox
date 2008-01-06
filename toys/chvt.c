/* vi: set sw=4 ts=4: */
/*
 * chvt.c switch virtual terminals
 * 
 * Copyright (C) 2008 David Anders <danders@amltd.com>
 *
 */

#include "toys.h"

#define VT_ACTIVATE	0x5606
#define VT_WAITACTIVE	0x5607

/* note get_console_fb() will need to be moved into a seperate lib section */
int get_console_fd()
{
    int fd;

    fd = open("/dev/console", O_RDWR);
    if (fd >= 0)
	return fd;

    fd = open("/dev/vc/0", O_RDWR);
    if (fd >= 0)
	return fd;

    fd = open("/dev/tty", O_RDWR);
    if (fd >= 0)
	return fd;

    return -1;
}

void chvt_main(void)
{
    int vtnum,fd;


    if(!*toys.optargs)
	return;

    vtnum=atoi(*toys.optargs);

    fd=get_console_fd();
    if (fd < 0)
	return;
	
    if (ioctl(fd,VT_ACTIVATE,vtnum))
	return;

    if (ioctl(fd,VT_WAITACTIVE,vtnum))
	return;

}
