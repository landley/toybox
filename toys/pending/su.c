/* su.c - switch user
 *
 * Copyright 2013 CE Strake <strake888@gmail.com>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/su.html

USE_SU(NEWTOY(su, "lmpc:s:", TOYFLAG_BIN))

config SU
  bool "su"
  default n
  help
    usage: su [-lmp] [-c cmd] [-s shell] [user [argu...]]

    Switch to given user, or root if not given, and call a shell with the given arguments.

    options:
      -s  shell to call
      -c  command to pass to shell with -c

    flags:
      -l      login shell
      -(m|p)  preserve environment
*/

#define FOR_su
#include "toys.h"

GLOBALS(
  char *sArgu;
  char *cArgu;
)

extern char **environ;

static void deny () {
  printf ("Denied\n");
  xexit ();
}

void su_main () {
  char *name, *passhash, **argu, **argv;
  struct passwd *up;
  struct spwd *shp;
  long ii;

  if (toys.optc && strcmp ("-", toys.optargs[0]) == 0) {
    toys.optflags |= FLAG_l;
    toys.optc--; toys.optargs++;
  }

  if (toys.optc) {
    name = toys.optargs[0];
    toys.optc--; toys.optargs++;
  }
  else name = "root";
  shp = getspnam (name);
  if (!shp) perror_exit ("failed to find password");

  switch (shp -> sp_pwdp[0]) {
  case '!': deny ();
  case '$': break;
  default : error_exit ("bad password format");
  }

  if (read_password (toybuf, sizeof (toybuf), "Password: ") != 0) perror_exit ("failed to read password");

  passhash = crypt (toybuf, shp -> sp_pwdp);
  if (!passhash) perror_exit ("failed to compute password hash");
  for (ii = 0; toybuf[ii]; ii++) toybuf[ii] = 0;

  if (strcmp (passhash, shp -> sp_pwdp) != 0) deny ();

  up = getpwnam (name);
  if (!up) perror_exit ("failed to getpwnam");

  if (setuid (up -> pw_uid) < 0) perror_exit ("failed to setuid");
  if (chdir  (up -> pw_dir) < 0) perror_exit ("failed to chdir");

  argu = xmalloc (sizeof (char *)*(toys.optc + 4));
  argv = argu;
  argv[0] = toys.optflags & FLAG_s ? TT.sArgu : up -> pw_shell;
  if (toys.optflags & FLAG_c) {
    argv[1] = toys.optflags & FLAG_l ? "-lc" : "-c";
    argv[2] = TT.cArgu;
    argv += 2;
  }
  else if (toys.optflags & FLAG_l) (argv++)[1] = "-l";
  for (ii = 0; ii < toys.optc; ii++) argv[ii + 1] = toys.optargs[ii];
  if (execve (argu[0], argu,
              toys.optflags & FLAG_l ? (char *[]){
                xmsprintf ( "HOME=%s", up -> pw_dir),
                xmsprintf ("SHELL=%s", up -> pw_shell),
                xmsprintf ( "USER=%s", up -> pw_name),
                xmsprintf ( "TERM=%s", getenv ("TERM")),
                0
              } : environ) < 0) perror_exit ("failed to exec %s", argu[0]);
}
