/* login.c - Start a session on the system.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * No support for PAM/securetty/selinux/login script/issue/utmp
 * Relies on libcrypt for hash calculation.
 *
 * TODO: this command predates "pending" but needs cleanup. It #defines
 * random stuff, calls exit() form a signal handler... yeah.

USE_LOGIN(NEWTOY(login, ">1f:ph:", TOYFLAG_BIN|TOYFLAG_NEEDROOT))

config LOGIN
  bool "login"
  default y
  depends on TOYBOX_SHADOW
  help
    usage: login [-p] [-h host] [-f USERNAME] [USERNAME]

    Log in as a user, prompting for username and password if necessary.

    -p	Preserve environment
    -h	The name of the remote host for this login
    -f	login as USERNAME without authentication
*/

#define FOR_login
#include "toys.h"

GLOBALS(
  char *hostname;
  char *username;

  int login_timeout, login_fail_timeout;
)

static void login_timeout_handler(int sig __attribute__((unused)))
{
  printf("\nLogin timed out after %d seconds.\n", TT.login_timeout);
  exit(0);
}

void login_main(void)
{
  char *forbid[] = {
    "BASH_ENV", "ENV", "HOME", "IFS", "LD_LIBRARY_PATH", "LD_PRELOAD",
    "LD_TRACE_LOADED_OBJECTS", "LD_BIND_NOW", "LD_AOUT_LIBRARY_PATH",
    "LD_AOUT_PRELOAD", "LD_NOWARN", "LD_KEEPDIR", "SHELL"
  };
  int hh = toys.optflags&FLAG_h, count, tty;
  char uu[33], *username, *pass = 0, *ss;
  struct passwd *pwd = 0;

  for (tty=0; tty<3; tty++) if (isatty(tty)) break;
  if (tty == 3) error_exit("no tty");

  for (count = 0; count < ARRAY_LEN(forbid); count++) unsetenv(forbid[count]);

  openlog("login", LOG_PID | LOG_CONS, LOG_AUTH);
  xsignal(SIGALRM, login_timeout_handler);

  if (TT.username) username = TT.username;
  else username = *toys.optargs;
  for (count = 0; count < 3; count++) {
    alarm(TT.login_timeout = 60);
    tcflush(0, TCIFLUSH);

    if (!username) {
      int i;

      memset(username = uu, 0, sizeof(uu));
      gethostname(uu, sizeof(uu)-1);
      printf("%s%slogin: ", *uu ? uu : "", *uu ? " " : "");
      fflush(stdout);

      if(!fgets(uu, sizeof(uu)-1, stdin)) _exit(1);

      // Remove trailing \n and so on
      for (i = 0; i<sizeof(uu); i++) if (uu[i]<=' ' || uu[i]==':') uu[i]=0;
      if (!*uu) {
        username = 0;
        continue;
      }
    }

    // If user exists and isn't locked
    pwd = getpwnam(username);
    if (pwd && *pwd->pw_passwd != '!' && *pwd->pw_passwd != '*') {

      // Pre-authenticated or passwordless
      if (TT.username || !*pwd->pw_passwd) break;

      // fetch shadow password if necessary
      if (*(pass = pwd->pw_passwd) == 'x') {
        struct spwd *spwd = getspnam (username);

        if (spwd) pass = spwd->sp_pwdp;
      }
    } else if (TT.username) error_exit("bad -f '%s'", TT.username);

    // Verify password. (Prompt for password _before_ checking disable state.)
    if (!read_password(toybuf, sizeof(toybuf), "Password: ")) {
      int x = pass && (ss = crypt(toybuf, pass)) && !strcmp(pass, ss);

      // password go bye-bye now.
      memset(toybuf, 0, sizeof(toybuf));
      if (x) break;
    }

    syslog(LOG_WARNING, "invalid password for '%s' on %s %s%s", pwd->pw_name,
      ttyname(tty), hh ? "from " : "", hh ? TT.hostname : "");

    sleep(3);
    puts("Login incorrect");

    username = 0;
    pwd = 0;
  }

  alarm(0);
  // This had password data in it, and we reuse for motd below
  memset(toybuf, 0, sizeof(toybuf));

  if (!pwd) error_exit("max retries (3)");

  // Check twice because "this file exists" is a security test, and in
  // theory filehandle exhaustion or other error could make open/read fail.
  if (pwd->pw_uid && !access("/etc/nologin", R_OK)) {
    ss = readfile("/etc/nologin", toybuf, sizeof(toybuf));
    puts ((ss && *ss) ? ss : "nologin");
    free(ss);
    toys.exitval = 1;

    return;
  }

  xsetuser(pwd);

  if (chdir(pwd->pw_dir)) printf("bad $HOME: %s\n", pwd->pw_dir);

  if (!(toys.optflags&FLAG_p)) {
    char *term = getenv("TERM");

    clearenv();
    if (term) setenv("TERM", term, 1);
  }

  setenv("USER", pwd->pw_name, 1);
  setenv("LOGNAME", pwd->pw_name, 1);
  setenv("HOME", pwd->pw_dir, 1);
  setenv("SHELL", pwd->pw_shell, 1);

  // Message of the day
  if ((ss = readfile("/etc/motd", 0, 0))) {
    puts(ss);
    free(ss);
  }

  syslog(LOG_INFO, "%s logged in on %s %s %s", pwd->pw_name,
    ttyname(tty), hh ? "from" : "", hh ? TT.hostname : "");

  // not using xexec(), login calls absolute path from filesystem so must exec()
  execl(pwd->pw_shell, xmprintf("-%s", pwd->pw_shell), (char *)0);
  perror_exit("exec shell '%s'", pwd->pw_shell);
}
