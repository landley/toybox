/* pwd.c - Print working directory.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/echo.html

USE_PWD(NEWTOY(pwd, ">0LP[!LP]", TOYFLAG_BIN))

config PWD
  bool "pwd"
  default y
  help
    usage: pwd [-L|-P]

    The print working directory command prints the current directory.

    -P  Avoid all symlinks
    -L  Use the value of the environment variable "PWD" if valid

    The option "-L" is implied by default.
*/

#define FOR_pwd
#include "toys.h"

void pwd_main(void)
{
  char *pwd = xgetcwd(), *env_pwd;
  struct stat st[2];

  if (!(toys.optflags & FLAG_P) && (env_pwd = getenv("PWD")) &&
    !stat(pwd, &st[0]) && !stat(env_pwd, &st[1]) &&
    (st[0].st_ino == st[1].st_ino)) xprintf("%s\n", env_pwd);
  else xprintf("%s\n", pwd);
  if (CFG_TOYBOX_FREE) free(pwd);
}
