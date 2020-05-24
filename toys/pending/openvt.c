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
    usage: openvt [-c NUM] [-sw] [COMMAND...]

    Start a program on a new virtual terminal.

    -c NUM  Use VT NUM
    -s    Switch to new VT
    -w    Wait for command to exit

    Together -sw switch back to originating VT when command completes.

config DEALLOCVT
  bool "deallocvt"
  default n
  help
    usage: deallocvt [NUM]

    Deallocate unused virtual terminals, either a specific /dev/ttyNUM, or all.
*/

#define FOR_openvt
#include "toys.h"
#include <linux/vt.h>
#include <linux/kd.h>

GLOBALS(
  long c;
)

int open_console(void)
{
  char arg = 0, *console_name[] = {"/dev/tty", "/dev/tty0", "/dev/console"};
  int i, fd;

  for (i = 0; i < ARRAY_LEN(console_name); i++) {
    if (0>(fd = open(console_name[i], O_RDWR))) continue;
    if (!ioctl(fd, KDGKBTYPE, &arg)) return fd;
    close(fd);
  }
  for (fd = 0; fd < 3; fd++) if (!ioctl(fd, KDGKBTYPE, &arg)) return fd;
  error_exit("can't open console");
}

void openvt_main(void)
{
  struct vt_stat vstate;
  int fd;
  pid_t pid;

  // find current console
  if (-1 == (ioctl(fd = open_console(), VT_GETSTATE, &vstate)) ||
      (!TT.c && 0>=(TT.c = xioctl(fd, VT_OPENQRY, &fd))))
    perror_exit("can't find open VT");

  sprintf(toybuf, "/dev/tty%ld", TT.c);
  close(0);  //new vt becomes stdin
  dup2(dup2(xopen_stdio(toybuf, O_RDWR), 1), 2);
  if (FLAG(s)) {
    ioctl(0, VT_ACTIVATE, (int)TT.c);
    ioctl(0, VT_WAITACTIVE, (int)TT.c);
  }

  if (!(pid = xfork())) {
    setsid();
    ioctl(0, TIOCSCTTY, 0);
    if (fd>2) close(fd);
    xexec(toys.optargs);
  }

  if (FLAG(w)) {
    while (-1 == waitpid(pid, NULL, 0) && errno == EINTR);
    if (FLAG(s)) {
      ioctl(fd, VT_ACTIVATE, vstate.v_active);
      ioctl(fd, VT_WAITACTIVE, vstate.v_active);
      ioctl(fd, VT_DISALLOCATE, (int)TT.c);
    }
  }
  close(fd);
}

void deallocvt_main(void)
{
  int fd, vt_num = 0; // 0 = all

  if (*toys.optargs) vt_num = atolx_range(*toys.optargs, 1, 63);
  if (-1 == ioctl(fd = open_console(), VT_DISALLOCATE, vt_num))
    perror_exit("%d", vt_num);
  close(fd);
}
