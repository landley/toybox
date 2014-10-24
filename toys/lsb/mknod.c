/* mknod.c - make block or character special file
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/mknod.html

USE_MKNOD(NEWTOY(mknod, "<2>4m(mode):"USE_MKNOD_SMACK("Z:"), TOYFLAG_BIN|TOYFLAG_UMASK))

config MKNOD
  bool "mknod"
  default y
  help
    usage: mknod [-m MODE] NAME TYPE [MAJOR MINOR]

    Create a special file NAME with a given type. TYPE is b for block device,
    c or u for character device, p for named pipe (which ignores MAJOR/MINOR).

    -m	Mode (file permissions) of new device, in octal or u+x format

config MKNOD_SMACK
  bool
  default y
  depends on PS && TOYBOX_SMACK
  help
    usage: mknod [-Z CONTEXT] ...

    -Z	Set security context to created file
*/

#define FOR_mknod
#include "toys.h"

GLOBALS(
  char *m;
  char *arg_context;
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

  if (mknod(toys.optargs[0], mode | modes[type], makedev(major, minor))) {
    perror_exit("mknod %s failed", toys.optargs[0]);
  }
  if (CFG_MKNOD_SMACK) {
    if (toys.optflags & FLAG_Z) {
      if (smack_set_label_for_path(toys.optargs[0], XATTR_NAME_SMACK, 0, TT.arg_context) < 0) {
        unlink(toys.optargs[0]);
        error_exit("Unable to create node '%s' with '%s' as context.", toys.optargs[0], TT.arg_context);
      }
    }
  }
}
