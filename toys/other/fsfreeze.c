/* fsfreeze.c - freeze or thaw filesystem
 *
 * No standard.

USE_FSFREEZE(NEWTOY(fsfreeze, "<1>1f|u|[!fu]", TOYFLAG_USR|TOYFLAG_SBIN))

config FSFREEZE
  bool "fsfreeze"
  default y
  help
    usage: fsfreeze {-f | -u} MOUNTPOINT

    Freeze or unfreeze a filesystem.

    -f	Freeze
    -u	Unfreeze
*/

#define FOR_fsfreeze
#define FIFREEZE _IOWR('X', 119, int)
#define FITHAW   _IOWR('X', 120, int)
#include "toys.h"

void fsfreeze_main(void)
{
  int fd = xopenro(*toys.optargs); 
  long p = 1;

  xioctl(fd, FLAG(f) ? FIFREEZE : FITHAW, &p);
  xclose(fd);
}
