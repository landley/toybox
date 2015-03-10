/* mknod.c - make block or character special file
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/mknod.html

USE_MKNOD(NEWTOY(mknod, "<2>4m(mode):", TOYFLAG_BIN|TOYFLAG_UMASK))

config MKNOD
  bool "mknod"
  default y
  help
    usage: mknod [-m MODE] NAME TYPE [MAJOR MINOR]

    Create a special file NAME with a given type. TYPE is b for block device,
    c or u for character device, p for named pipe (which ignores MAJOR/MINOR).

    -m	Mode (file permissions) of new device, in octal or u+x format
*/

#define FOR_mknod
#include "toys.h"

GLOBALS(
  char *m;
)

void mknod_main(void)
{
  mode_t modes[] = {S_IFIFO, S_IFCHR, S_IFCHR, S_IFBLK};
  int major=0, minor=0, type;
  int mode = TT.m ? string_to_mode(TT.m, 0777) : 0660;

  type = stridx("pcub", *toys.optargs[1]);
  if (type == -1) perror_exit("bad type '%c'", *toys.optargs[1]);
  if (type) {
    if (toys.optc != 4) perror_exit("need major/minor");

    major = atoi(toys.optargs[2]);
    minor = atoi(toys.optargs[3]);
  }

  if (mknod(toys.optargs[0], mode | modes[type], makedev(major, minor)))
    perror_exit("mknod %s failed", toys.optargs[0]);
}
