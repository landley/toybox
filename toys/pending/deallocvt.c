/* deallocvt.c - Deallocate virtual terminal(s)
 *
 * Copyright 2014 Vivek Kumar Bhagat <vivek.bhagat89@gmail.com>
 *
 * No Standard.

USE_DEALLOCVT(NEWTOY(deallocvt, ">1", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_NEEDROOT))

config DEALLOCVT
  bool "deallocvt"
  depends on OPENVT
  default n
  help
    usage: deallocvt [N]

    Deallocate unused virtual terminal /dev/ttyN
    default value of N is 0, deallocate all unused consoles
*/

#include "toys.h"
#include <linux/vt.h>

void deallocvt_main(void)
{
  int fd;

  // 0 : deallocate all unused consoles
  int vt_num = 0;

  if (toys.optargs[0])
    vt_num = atolx_range(toys.optargs[0], 1, 63);

  fd = find_console_fd();
  if (fd < 0)  error_exit("can't open console");

  xioctl(fd, VT_DISALLOCATE, (void *)(ptrdiff_t)vt_num);
}
