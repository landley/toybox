/* su.c - switch user
 *
 * Copyright 2013 CE Strake <strake888@gmail.com>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/su.html
 * TODO: log su attempts

USE_SU(NEWTOY(su, "lmpc:s:", TOYFLAG_BIN|TOYFLAG_ROOTONLY))

config SU
  bool "su"
  default y
  depends on TOYBOX_SHADOW
  help
    usage: su [-lmp] [-c CMD] [-s SHELL] [USER [ARGS...]]

    Switch to user (or root) and run shell (with optional command line).

    -s	shell to use
    -c	command to pass to shell with -c
    -l	login shell
    -(m|p)	preserve environment
*/

#define FOR_su
#include "toys.h"

GLOBALS(
  char *s;
  char *c;
)

static char *snapshot_env(char *name)
{
  char *s = getenv(name);

  if (s) return xmprintf("%s=%s", name, s);

  return 0;
}

void su_main()
{
  char *name, *passhash = 0, **argu, **argv;
  struct passwd *up;
  struct spwd *shp;

  if (*toys.optargs && !strcmp("-", *toys.optargs)) {
    toys.optflags |= FLAG_l;
    toys.optargs++;
  }

  if (*toys.optargs) name = *(toys.optargs++);
  else name = "root";

  if (!(shp = getspnam(name))) perror_exit("no '%s'", name);
  if (getuid()) {
    if (*shp->sp_pwdp != '$') goto deny;
    if (read_password(toybuf, sizeof(toybuf), "Password: ")) goto deny;
    passhash = crypt(toybuf, shp->sp_pwdp);
    memset(toybuf, 0, sizeof(toybuf));
    if (!passhash || strcmp(passhash, shp->sp_pwdp)) goto deny;
  }

  up = xgetpwnam(name);
  xsetuser(up);

  argv = argu = xmalloc(sizeof(char *)*(toys.optc + 4));
  *(argv++) = TT.s ? TT.s : up->pw_shell;

  if (toys.optflags & FLAG_l) {
    int i;
    char *stuff[] = {snapshot_env("TERM"), snapshot_env("DISPLAY"),
      snapshot_env("COLORTERM"), snapshot_env("XAUTHORITY")};

    clearenv();
    for (i=0; i < ARRAY_LEN(stuff); i++) if (stuff[i]) putenv(stuff[i]);
    *(argv++) = "-l";
    xchdir(up->pw_dir);
  } else unsetenv("IFS");
  setenv("PATH", "/sbin:/bin:/usr/sbin:/usr/bin", 1);
  if (!(toys.optflags & (FLAG_m|FLAG_p))) {
    setenv("HOME", up->pw_dir, 1);
    setenv("SHELL", up->pw_shell, 1);
    setenv("USER", up->pw_name, 1);
    setenv("LOGNAME", up->pw_name, 1);
  } else unsetenv("IFS");

  if (toys.optflags & FLAG_c) {
    *(argv++) = "-c";
    *(argv++) = TT.c;
  }
  while ((*(argv++) = *(toys.optargs++)));
  xexec(argu);

deny:
  puts("No.");
  toys.exitval = 1;
}
