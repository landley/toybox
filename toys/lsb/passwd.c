/* passwd.c - Program to update user password.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 * Modified 2012 Jason Kyungwan Han <asura321@gmail.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/passwd.html

USE_PASSWD(NEWTOY(passwd, ">1a:dlu", TOYFLAG_STAYROOT|TOYFLAG_USR|TOYFLAG_BIN))

config PASSWD
  bool "passwd"
  default y
  depends on TOYBOX_SHADOW
  help
    usage: passwd [-a ALGO] [-dlu] <account name>

    update user's authentication tokens. Default : current user

    -a ALGO	Encryption method (des, md5, sha256, sha512) default: des
    -d		Set password to ''
    -l		Lock (disable) account
    -u		Unlock (enable) account

config PASSWD_SAD
  bool "Add sad password checking heuristics"
  default n
  depends on PASSWD
  help
    Password changes are checked to make sure they don't include the entire
    username (but not a subset of it), and the entire previous password
    (but changing password1, password2, password3 is fine). This heuristic
    accepts "aaaaaa" as a password.
*/

#define FOR_passwd
#include "toys.h"

GLOBALS(
  char *algo;
)

static int str_check(char *s, char *p)
{
  if (strnstr(s, p) || strnstr(p, s)) return 1;
  return 0;
}

// Insane heuristic won't find password1 password2 password3...?
static void strength_check(char *newp, char *oldp, char *user)
{
  char *msg = NULL;

  if (strlen(newp) < 6) { //Min passwd len
    msg = "too short";
    xprintf("BAD PASSWORD: %s\n",msg);
  }
  if (!newp[0]) return; //passwd is empty

  if (str_check(newp, user)) {
    msg = "user based password";
    xprintf("BAD PASSWORD: %s\n",msg);
  }

  if (oldp[0] && str_check(newp, oldp)) {
    msg = "based on old passwd";
    xprintf("BAD PASSWORD: %s\n",msg);
  }
}

static int verify_passwd(char * pwd)
{
  char * pass;

  if (!pwd) return 1;
  if (pwd[0] == '!' || pwd[0] == '*') return 1;

  pass = crypt(toybuf, pwd);
  if (pass  && !strcmp(pass, pwd)) return 0;

  return 1;
}

static char *new_password(char *oldp, char *user)
{
  char *newp = NULL;

  if (read_password(toybuf, sizeof(toybuf), "New password:"))
    return NULL; //may be due to Ctrl-C

  newp = xstrdup(toybuf);
  if (CFG_PASSWD_SAD) strength_check(newp, oldp, user);
  if (read_password(toybuf, sizeof(toybuf), "Retype password:")) {
    free(newp);
    return NULL; //may be due to Ctrl-C
  }

  if (!strcmp(newp, toybuf)) return newp;
  else error_msg("Passwords do not match.\n");
  // Failure Case
  free(newp);
  return NULL;
}

void passwd_main(void)
{
  uid_t myuid;
  struct passwd *pw;
  struct spwd *sp;
  char *name = NULL, *pass = NULL, *encrypted = NULL, *newp = NULL,
       *orig = (char *)"", salt[MAX_SALT_LEN];
  int ret = -1;

  myuid = getuid();
  if (myuid && (toys.optflags & (FLAG_l | FLAG_u | FLAG_d)))
    error_exit("Not root");

  pw = xgetpwuid(myuid);

  if (*toys.optargs) name = toys.optargs[0];
  else name = xstrdup(pw->pw_name);

  pw = xgetpwnam(name);

  if (myuid && (myuid != pw->pw_uid)) error_exit("Not root");

  pass = pw->pw_passwd;
  if (pw->pw_passwd[0] == 'x') {
    //get shadow passwd
    sp = getspnam(name);
    if (sp) pass = sp->sp_pwdp;
  }


  if (!(toys.optflags & (FLAG_l | FLAG_u | FLAG_d))) {
    
    if (!(toys.optflags & FLAG_a)) TT.algo = "des";
    if (get_salt(salt, TT.algo) == -1)
      error_exit("Error: Unkown encryption algorithm\n");

    printf("Changing password for %s\n",name);
    if (myuid && pass[0] == '!')
      error_exit("Can't change, password is locked for %s",name);
    if (myuid) {
      //Validate user 

      if (read_password(toybuf, sizeof(toybuf), "Origial password:")) {
        if (!toys.optargs[0]) free(name);
        return;
      }
      orig = toybuf;
      if (verify_passwd(pass)) error_exit("Authentication failed\n");
    }

    orig = xstrdup(orig);

    // Get new password
    newp = new_password(orig, name);
    if (!newp) {
      free(orig);
      if (!toys.optargs[0]) free(name);
      return; //new password is not set well.
    }

    encrypted = crypt(newp, salt);
    free(newp);
    free(orig);
  } else if (toys.optflags & FLAG_l) {
    if (pass[0] == '!') error_exit("password is already locked for %s",name);
    printf("Locking password for %s\n",name);
    encrypted = xmprintf("!%s",pass);
  } else if (toys.optflags & FLAG_u) {
    if (pass[0] != '!') error_exit("password is already unlocked for %s",name);

    printf("Unlocking password for %s\n",name);
    encrypted = xstrdup(&pass[1]);
  } else if (toys.optflags & FLAG_d) {
    printf("Deleting password for %s\n",name);
    encrypted = xstrdup(""); //1 = "", 2 = '\0'
  }

  // Update the passwd
  if (pw->pw_passwd[0] == 'x')
    ret = update_password("/etc/shadow", name, encrypted);
  else ret = update_password("/etc/passwd", name, encrypted);

  if ((toys.optflags & (FLAG_l | FLAG_u | FLAG_d))) free(encrypted);

  if (!toys.optargs[0]) free(name);
  if (!ret) error_msg("Success");
  else error_msg("Failure");
}
