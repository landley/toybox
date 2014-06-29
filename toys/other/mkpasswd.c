/* mkpasswd.c - encrypt the given passwd using salt
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard

USE_MKPASSWD(NEWTOY(mkpasswd, ">2S:m:P#=0<0", TOYFLAG_USR|TOYFLAG_BIN))

config MKPASSWD
  bool "mkpasswd"
  default y
  help
    usage: mkpasswd [-P FD] [-m TYPE] [-S SALT] [PASSWORD] [SALT]

    Crypt PASSWORD using crypt(3)

    -P FD   Read password from file descriptor FD
    -m TYPE Encryption method (des, md5, sha256, or sha512; default is des)
    -S SALT
*/

#define FOR_mkpasswd
#include "toys.h"

GLOBALS(
  long pfd;
  char *method;
  char *salt;
)

void mkpasswd_main(void)
{
  char salt[MAX_SALT_LEN] = {0,};
  int i;

  if (!TT.method) TT.method = "des";
  if (toys.optc == 2) {
    if (TT.salt) error_exit("duplicate salt");
    TT.salt = toys.optargs[1];
  }

  if (-1 == (i = get_salt(salt, TT.method))) error_exit("bad -m");
  if (TT.salt) {
    char *s = TT.salt;

    // In C locale, isalnum() means [A-Za-Z0-0]
    while (isalnum(*s) || *s == '.' || *s == '/') s++;
    if (*s) error_exit("salt not in [./A-Za-z0-9]");

    snprintf(salt+i, sizeof(salt)-i, "%s", TT.salt);
  }

  // Because read_password() doesn't have an fd argument
  if (TT.pfd) {
    if (dup2(TT.pfd, 0) == -1) perror_exit("fd");
    close(TT.pfd);
  }

  // If we haven't got a password on the command line, read it from tty or FD
  if (!*toys.optargs) {
    // Prompt and read interactively?
    if (isatty(0)) {
      if (read_password(toybuf, sizeof(toybuf), "Password: ")) 
        perror_exit("password read failed");
    } else {
      for (i = 0; i<sizeof(toybuf)-1; i++) {
        if (!xread(0, toybuf+i, 1)) break;
        if (toybuf[i] == '\n' || toybuf[i] == '\r') break;
      }
      toybuf[i] = 0;
    }
  }

  // encrypt & print the password
  xprintf("%s\n",crypt(*toys.optargs ? *toys.optargs : toybuf, salt));
}
