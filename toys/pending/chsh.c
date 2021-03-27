/* chsh.c - Change login shell.
 *
 * Copyright 2021 Michael Christensen
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/chsh.html

USE_CHSH(NEWTOY(chsh, "s:", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_STAYROOT))

config CHSH
  bool "chsh"
  default y
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
  int i;
  FILE *file;
  size_t size, buf_size;
  char *user, *line, *shell, *password, *encrypted;
  struct passwd *passwd_info;
  struct spwd *shadow_info;

  // Use max login name size for buffer size
  if (-1 == (buf_size = sysconf(_SC_LOGIN_NAME_MAX))) buf_size = 256;

  if (!(user = malloc(buf_size * sizeof(user)))) perror_exit("Failed to allocate memory");
  if (!(shell = malloc(buf_size * sizeof(shell)))) perror_exit("Failed to allocate memory");
  if (!(line = malloc(buf_size * sizeof(line)))) perror_exit("Failed to allocate memory");
  if (MAP_FAILED == (password = mmap(NULL, buf_size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED | MAP_NORESERVE, -1, 0))) perror_exit("Failed to get memory map");

  // Get uid user information, may be discarded later
  if (!(passwd_info = getpwuid(getuid()))) perror_exit("Failed to get passwd record");

  if ((user = *toys.optargs)) {
    errno = 0;
    if (!(passwd_info = getpwnam(user)) && !errno) error_exit("Failed to get user info");

    // Are we either root or changing our own shell?
    if (getuid() && strcmp(passwd_info->pw_name, user)) error_exit("Permission denied\n");
  } else user = passwd_info->pw_name;

  // Get a password, encrypt it, wipe it, and check it
  if (!(shadow_info = getspnam(passwd_info->pw_name))) perror_exit("Failed to get shadow record");
  if (!read_password(password, buf_size, "Password: ")) error_exit("Failed to read password\n");
  if (!(encrypted = crypt(password, shadow_info->sp_pwdp))) perror_exit("Failed to encrypt password");
  memset(password, 0, buf_size);
  if (!munmap(password, buf_size)) perror_exit("Failed to unmap memory");
  if (strcmp(encrypted, shadow_info->sp_pwdp)) perror_exit("Incorrect password");

  // Get new shell (either -s or interactive)
  if (!(file = fopen("/etc/shells", "r"))) perror_exit("Failed to open /etc/shells");
  if (toys.optflags) shell = TT.s;
  else {
    xprintf("Changing the login shell for %s\nEnter the new value, or press ENTER for default\n    Login shell [%s]: ", user, passwd_info->pw_shell);

    errno = 0; size = 0;
    while (EOF != (i = fgetc(stdin))) {
      if (errno) perror_exit("Failed to read character from stdin");

      if ('\n' != i) *(shell + size++) = i;
      else {
        *(shell + size) = '\0'; break;
      }
    }
  }

  // Is shell in /etc/shells?
  if (strlen(shell)) {
    line = NULL; size = 0; i = 0; errno = 0;

    while (EOF != getline(&line, &size, file)) {
      if (errno) perror_exit("Failed to read from /etc/shells");

      size = strlen(line) - 1;
      if ('\n' == *(line + size)) *(line + size) = '\0';

      if (!strcmp(shell, line)) {
        i = 1; break;
      }
    }

    if (!i) error_exit("Shell not found in '/etc/shells'");
  } else {

    // Get default shell, ignoring comments and blank lines
    do {
      shell = NULL;
      if (-1 == getline(&shell, &size, file)) perror_exit("Failed to read from /etc/shells");
    } while (*shell != '/');

    size = strlen(shell) - 1;
    if ('\n' == *(shell + size)) *(shell + size) = '\0';
  }

  // Update shell and write passwd entry to tempfile
  strncpy(passwd_info->pw_shell, shell, buf_size);
  if (!(file = tmpfile())) perror_exit("Failed to create tempfile");
  if (!putpwent(passwd_info, file)) perror_exit("Failed to write to passwd entry");

  // Move passwd entry from file to string
  if (-1 == (i = ftell(file))) perror_exit("Failed to get tempfile offset");
  if (buf_size < i && !realloc(line, i)) perror_exit("Failed to reallocate memory");
  rewind(file);
  if (fread(line, 1, i, file) < i) perror_exit("Failed to read from tempfile");

  // Update /etc/passwd using that string
  if (-1 == update_password("/etc/passwd", user, NULL)) perror_exit("Failed to remove passwd entry");
  if (-1 == update_password("/etc/passwd", user, line)) perror_exit("Failed to add passwd entry");
}
