/* chsh.c - Change login shell.
 *
 * Copyright 2021 Michael Christensen
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/chsh.html

USE_CHSH(NEWTOY(chsh, "s:", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_STAYROOT))

config CHSH
  bool "chsh"
  default n
  help
    usage: chsh [-s SHELL] [USER]

    Change user's login shell.

    -s	Use SHELL instead of prompting

    Non-root users can only change their own shell to one listed in /etc/shells.
*/

#define FOR_chsh
#include "toys.h"

GLOBALS(
  char *s;
)

void chsh_main()
{
  FILE *file;
  char *user, *line, *shell, *encrypted;
  struct passwd *passwd_info;
  struct spwd *shadow_info;

  // Get uid user information, may be discarded later

  if ((user = *toys.optargs)) {
    passwd_info = xgetpwnam(user);
    if (geteuid() && strcmp(passwd_info->pw_name, user))
      error_exit("Permission denied\n");
  } else {
    passwd_info = xgetpwuid(getuid());
    user = passwd_info->pw_name;
  }

  // Get a password, encrypt it, wipe it, and check it
  if (mlock(toybuf, sizeof(toybuf))) perror_exit("mlock");
  if (!(shadow_info = getspnam(passwd_info->pw_name))) perror_exit("getspnam");
  if (read_password(toybuf, sizeof(toybuf), "Password: ")) perror_exit("woaj"); //xexit();
  if (!(encrypted = crypt(toybuf, shadow_info->sp_pwdp))) perror_exit("crypt");
  memset(toybuf, 0, sizeof(toybuf));
  munlock(toybuf, sizeof(toybuf)); // prevents memset from "optimizing" away.
  if (strcmp(encrypted, shadow_info->sp_pwdp)) perror_exit("Bad password");

  // Get new shell (either -s or interactive)
  file = xfopen("/etc/shells", "r");
  if (toys.optflags) shell = TT.s;
  else {
    xprintf("Changing the login shell for %s\n"
            "Enter the new value, or press ENTER for default\n"
            "    Login shell [%s]: ", user, passwd_info->pw_shell);
    if (!(shell = xgetline(stdin))) xexit();
  }

  // Verify supplied shell in /etc/shells, or get default shell
  if (strlen(shell))
    while ((line = xgetline(file)) && strcmp(shell, line)) free(line);
  else do line = xgetline(file); while (line && *line != '/');
  if (!line) error_exit("Shell not found in '/etc/shells'");

  // Update /etc/passwd
  passwd_info->pw_shell = line;
  if (-1 == update_password("/etc/passwd", user, NULL)) perror_exit("Failed to remove passwd entry");
  file = xfopen("/etc/passwd", "a");
  if (putpwent(passwd_info, file)) perror_exit("putwent");
}
