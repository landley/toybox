/* mkpasswd.c - encrypt the given passwd using salt
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard

USE_MKPASSWD(NEWTOY(mkpasswd, ">2S:m:P#=0<0", TOYFLAG_USR|TOYFLAG_BIN))

config MKPASSWD
  bool "mkpasswd"
  default n
  help
    usage: mkpasswd [OPTIONS] [PASSWORD] [SALT]

    Crypt PASSWORD using crypt(3)

    -P N    Read password from fd N
    -m TYPE Encryption method, when TYPE='help', then show the methods available
    -S SALT
*/

#define FOR_mkpasswd
#include "toys.h"
#include "lib/xregcomp.h"

GLOBALS(
  long pfd;
  char *method;
  char *salt;
)


/*
 * validate the salt provided by user.
 * the allowed character set for salt is [./A-Za-z0-9]
 */
static void is_salt_valid(char *salt)
{
  regex_t rp;
  regmatch_t rm[1];
  char *regex = "[./A-Za-z0-9]*"; //salt REGEX

  xregcomp(&rp, regex, REG_NEWLINE);

  /* compare string against pattern --  remember that patterns 
     are anchored to the beginning of the line */
  if (regexec(&rp, salt, 1, rm, 0) == 0 && rm[0].rm_so == 0 
      && rm[0].rm_eo == strlen(salt))
      return;

  error_exit("salt should be in character set [./A-Za-z0-9]");
}

void mkpasswd_main(void)
{
  int offset = 0;
  char salt[MAX_SALT_LEN] = {0,};

  if (!(toys.optflags & FLAG_m)) TT.method = "des";
  else if (!strcmp(TT.method, "help")) {
    xprintf("Available encryption methods are:\n"
        " des\n md5\n sha256\n sha512\n");
    return;
  }
  // If arguments are there, then the second argument is Salt, can be NULL also
  if ((toys.optc == 2) && !(toys.optflags & FLAG_S)) TT.salt = toys.optargs[1];

  offset= get_salt(salt, TT.method);
  if (offset == -1) error_exit("unknown encryption method");
  if (TT.salt) {
    is_salt_valid(TT.salt);
    snprintf(salt + offset, MAX_SALT_LEN - offset, "%s", TT.salt);
  }

  if (toys.optflags & FLAG_P) {
    if (dup2(TT.pfd, STDIN_FILENO) == -1) perror_exit("fd");
    close(TT.pfd);
  }

  if (!toys.optc) {
    if (isatty(STDIN_FILENO)) {
      if (read_password(toybuf, sizeof(toybuf), "Password: ")) 
        perror_exit("password read failed");
    } else {
      // read from the given FD
      int i = 0;
      while (1) {
        int ret = read(0, &toybuf[i], 1);
        if ( ret < 0 ) perror_exit("password read failed");
        else if (ret == 0 || toybuf[i] == '\n' || toybuf[i] == '\r' ||
            sizeof(toybuf) == i+1) {
          toybuf[i] = '\0';
          break;
        }
        i++;
      }
    }
  } else snprintf(toybuf, sizeof(toybuf), "%s", toys.optargs[0]);

  // encrypt & print the password
  xprintf("%s\n",crypt(toybuf, salt));
}
