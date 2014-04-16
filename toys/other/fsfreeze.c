/* fsfreeze.c - freeze or thaw filesystem
 *
 * No standard.

USE_FSFREEZE(NEWTOY(fsfreeze, "<1>1f|u|[!fu]", TOYFLAG_USR|TOYFLAG_SBIN))

config FSFREEZE
  bool "fsfreeze"
  default y
  depends on TOYBOX_FIFREEZE
  help
    usage: fsfreeze {-f | -u} MOUNTPOINT

    Freeze or unfreeze a filesystem.

    -f	freeze
    -u	unfreeze
*/

#define FOR_fsfreeze
#include "toys.h"
#include <linux/fs.h>

void fsfreeze_main(void)
{
  int fd = xopen(*toys.optargs, O_RDONLY); 
  long p = 1;

  xioctl(fd, (toys.optflags & FLAG_f) ? FIFREEZE : FITHAW, &p);
  xclose(fd);
}
