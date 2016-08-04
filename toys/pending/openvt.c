/* openvt.c - Run a program on a new VT
 *
 * Copyright 2014 Vivek Kumar Bhagat <vivek.bhagat89@gmail.com>
 *
 * No Standard

USE_OPENVT(NEWTOY(openvt, "c#<1>63sw", TOYFLAG_BIN|TOYFLAG_NEEDROOT))
USE_DEALLOCVT(NEWTOY(deallocvt, ">1", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_NEEDROOT))

config OPENVT
  bool "openvt"
  default n
  depends on TOYBOX_FORK
  help
    usage: openvt [-c N] [-sw] [command [command_options]]

    start a program on a new virtual terminal (VT)

    -c N  Use VT N
    -s    Switch to new VT
    -w    Wait for command to exit

    if -sw used together, switch back to originating VT when command completes

config DEALLOCVT
  bool "deallocvt"
  default n
  help
    usage: deallocvt [N]

    Deallocate unused virtual terminal /dev/ttyN, or all unused consoles.
*/

#define FOR_openvt
#include "toys.h"
#include <linux/vt.h>
#include <linux/kd.h>

GLOBALS(
  unsigned long vt_num;
)

int open_console(void)
{
  char arg, *console_name[] = {"/dev/tty", "/dev/tty0", "/dev/console"};
  int i, fd;

  for (i = 0; i < ARRAY_LEN(console_name); i++) {
    fd = open(console_name[i], O_RDWR);
    if (fd >= 0) {
      arg = 0;
      if (!ioctl(fd, KDGKBTYPE, &arg)) return fd;
      close(fd);
    }
  }

  /* check std fd 0, 1 and 2 */
  for (fd = 0; fd < 3; fd++) {
    arg = 0;
    if (0 == ioctl(fd, KDGKBTYPE, &arg)) return fd;
  }

  return -1;
}

int xvtnum(int fd)
{
  int ret;

  ret = ioctl(fd, VT_OPENQRY, (int *)&TT.vt_num);
  if (ret != 0 || TT.vt_num <= 0) perror_exit("can't find open VT");

  return TT.vt_num;
}

void openvt_main(void)
{
  int fd, vt_fd, ret = 0;
  struct vt_stat vstate;
  pid_t pid;

  if (!(toys.optflags & FLAG_c)) {
    // check if fd 0,1 or 2 is already opened
    for (fd = 0; fd < 3; fd++)
      if (!ioctl(fd, VT_GETSTATE, &vstate)) {
        ret = xvtnum(fd);
        break;
      }

    // find VT number using /dev/console
    if (!ret) {
      fd = xopen("/dev/console", O_RDONLY | O_NONBLOCK);
      xioctl(fd, VT_GETSTATE, &vstate);
      xvtnum(fd);
    }
  }

  sprintf(toybuf, "/dev/tty%lu", TT.vt_num);
  fd = open_console();
  xioctl(fd, VT_GETSTATE, &vstate);

  close(0);  //new vt becomes stdin
  vt_fd = xopen_stdio(toybuf, O_RDWR);
  if (toys.optflags & FLAG_s) {
    ioctl(vt_fd, VT_ACTIVATE, TT.vt_num);
    ioctl(vt_fd, VT_WAITACTIVE, TT.vt_num);
  }

  close(1);
  close(2);
  dup2(vt_fd, 1);
  dup2(vt_fd, 2);
  while (vt_fd > 2)
    close(vt_fd--);

  pid = xfork();
  if (!pid) {
    setsid();
    ioctl(vt_fd, TIOCSCTTY, 0);
    xexec(toys.optargs);
  }

  if (toys.optflags & FLAG_w) {
    while (-1 == waitpid(pid, NULL, 0) && errno == EINTR)
      ;
    if (toys.optflags & FLAG_s) {
      ioctl(fd, VT_ACTIVATE, vstate.v_active);
      ioctl(fd, VT_WAITACTIVE, vstate.v_active);
      //check why deallocate isn't working here
      xioctl(fd, VT_DISALLOCATE, (void *)(ptrdiff_t)TT.vt_num); 
    }
  }
}

void deallocvt_main(void)
{
  long vt_num = 0; // 0 deallocates all unused consoles
  int fd;

  if (*toys.optargs) vt_num = atolx_range(*toys.optargs, 1, 63);

  if ((fd = open_console()) < 0) error_exit("can't open console");
  xioctl(fd, VT_DISALLOCATE, (void *)vt_num);
  if (CFG_TOYBOX_FREE) close(fd);
}
