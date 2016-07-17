/* setfattr.c - Write POSIX extended attributes.
 *
 * Copyright 2016 Android Open Source Project.
 *
 * No standard

USE_SETFATTR(NEWTOY(setfattr, "hn:v:x:[!xv]", TOYFLAG_USR|TOYFLAG_BIN))

config SETFATTR
  bool "setfattr"
  default y
  help
    usage: setfattr [-h] -n NAME [-v VALUE] FILE...
    usage: setfattr [-h] -x NAME FILE...

    Write POSIX extended attributes.

    -h	Do not dereference symbolic links.
    -n	Set value of given attribute.
    -x	Remove value of given attribute.
    -v	Value to use with -n (default is empty).
*/

#define FOR_setfattr
#include "toys.h"

GLOBALS(
  char *x, *v, *n;
)

static void do_setfattr(char *file)
{
  int (*setter)(const char *, const char *, const void *, size_t, int) =
      setxattr;
  int (*remover)(const char *, const char *) = removexattr;

  if (toys.optflags&FLAG_h) {
    setter = lsetxattr;
    remover = lremovexattr;
  }

  if (toys.optflags&FLAG_x) {
    if (remover(file, TT.x)) perror_msg("removexattr failed");
  } else {
    if (setter(file, TT.n, TT.v, TT.v ? strlen(TT.v) : 0, 0))
      perror_msg("setxattr failed");
  }
}

void setfattr_main(void)
{
  char **s;

  if (!(toys.optflags&(FLAG_n|FLAG_x))) error_exit("need 'n' or 'x'");
  for (s=toys.optargs; *s; s++) do_setfattr(*s);
}
