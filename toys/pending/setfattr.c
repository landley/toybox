/* setfattr.c - Write POSIX extended attributes.
 *
 * Copyright 2016 Android Open Source Project.
 *
 * No standard

USE_SETFATTR(NEWTOY(setfattr, "hn:|v:x:|[!xv]", TOYFLAG_USR|TOYFLAG_BIN))

config SETFATTR
  bool "setfattr"
  default n
  help
    usage: setfattr [-h] [-x|-n NAME] [-v VALUE] FILE...

    Write POSIX extended attributes.

    -h	Do not dereference symlink
    -n	Set given attribute
    -x	Remove given attribute
    -v	Set value for attribute -n (default is empty)
*/

#define FOR_setfattr
#include "toys.h"

GLOBALS(
  char *x, *v, *n;
)

static void do_setfattr(char *file)
{
  int h = toys.optflags & FLAG_h;

  if (toys.optflags&FLAG_x) {
    if ((h ? lremovexattr : removexattr)(file, TT.x))
      perror_msg("removexattr failed");
  } else 
    if ((h ? lsetxattr : setxattr)(file, TT.n, TT.v, TT.v?strlen(TT.v):0, 0))
      perror_msg("setxattr failed");
}

void setfattr_main(void)
{
  char **s;

  for (s=toys.optargs; *s; s++) do_setfattr(*s);
}
