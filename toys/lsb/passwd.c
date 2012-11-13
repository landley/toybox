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
  help
    usage: passwd [-a ALGO] [-d] [-l] [-u] <account name>

    update user’s authentication tokens. Default : current user

    -a ALGO	Encryption method (des, md5, sha256, sha512) default: des
    -d		Set password to ''
    -l		Lock (disable) account
    -u		Unlock (enable) account
*/

#define FOR_passwd
#include "toys.h"
#include <time.h>

GLOBALS(
  char *algo;
)

#define MAX_SALT_LEN  20 //3 for id, 16 for key, 1 for '\0'
#define URANDOM_PATH    "/dev/urandom"

#ifndef _GNU_SOURCE
char *strcasestr(const char *haystack, const char *needle);
#endif

unsigned int random_number_generator(int fd)
{
  unsigned int randnum;
  xreadall(fd, &randnum, sizeof(randnum));
  return randnum;
}

char inttoc(int i)
{
  // salt value uses 64 chracters in "./0-9a-zA-Z"
  const char character_set[]="./0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  i &= 0x3f; // masking for using 10 bits only
  return character_set[i];
}

int get_salt(char *salt)
{
  int i, salt_length = 0;
  int randfd;
  if(!strncmp(TT.algo,"des",3)){
    // 2 bytes salt value is used in des
    salt_length = 2;
  } else {
    *salt++ = '$';
    if(!strncmp(TT.algo,"md5",3)){
      *salt++ = '1';
      // 8 bytes salt value is used in md5
      salt_length = 8;
    } else if(!strncmp(TT.algo,"sha256",6)){
      *salt++ = '5';
      // 16 bytes salt value is used in sha256
      salt_length = 16;
    } else if(!strncmp(TT.algo,"sha512",6)){
      *salt++ = '6';
      // 16 bytes salt value is used in sha512
      salt_length = 16;
    } else return 1;

    *salt++ = '$';
  }

  randfd = xopen(URANDOM_PATH, O_RDONLY);
  for(i=0; i<salt_length; i++)
    salt[i] = inttoc(random_number_generator(randfd));
  salt[salt_length+1] = '\0';
  xclose(randfd);

  return 0;
}

static int str_check(char *s, char *p)
{
  if((strcasestr(s, p) != NULL) || (strcasestr(p, s) != NULL))
    return 1;
  return 0;
}

static void strength_check(char *newp, char *oldp, char *user)
{
  char *msg = NULL;
  if(strlen(newp) < 6) { //Min passwd len
    msg = "too short";
    xprintf("BAD PASSWORD: %s\n",msg);
  }
  if(!newp[0])
    return; //passwd is empty

  if(str_check(newp, user)) {
    msg = "user based password";
    xprintf("BAD PASSWORD: %s\n",msg);
  }

  if(oldp[0] && str_check(newp, oldp)) {
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
  if (pass != NULL && strcmp(pass, pwd)==0)
    return 0;

  return 1;
}

static char *new_password(char *oldp, char *user)
{
  char *newp = NULL;

  if(read_password(toybuf, sizeof(toybuf), "New password:"))
    return NULL; //may be due to Ctrl-C

  newp = xstrdup(toybuf);
  strength_check(newp, oldp, user);
  if(read_password(toybuf, sizeof(toybuf), "Retype password:")) {
    free(newp);
    return NULL; //may be due to Ctrl-C
  }

  if(strcmp(newp, toybuf) == 0)
    return newp;
  else error_msg("Passwords do not match.\n");
  /*Failure Case */
  free(newp);
  return NULL;
}


void passwd_main(void)
{
  uid_t myuid;
  struct passwd *pw;
  struct spwd *sp;
  char *name = NULL;
  char *pass = NULL, *encrypted = NULL, *newp = NULL;
  char *orig = (char *)"";
  char salt[MAX_SALT_LEN];
  int ret = -1;

  myuid = getuid();
  if((myuid != 0) && (toys.optflags & (FLAG_l | FLAG_u | FLAG_d)))
    error_exit("You need to be root to do these actions\n");

  pw = getpwuid(myuid);

  if(!pw)
    error_exit("Unknown uid '%u'",myuid);

  if(toys.optargs[0])
    name = toys.optargs[0];
  else
    name = xstrdup(pw->pw_name);

  pw = getpwnam(name);
  if(!pw) error_exit("Unknown user '%s'",name);

  if(myuid != 0 && (myuid != pw->pw_uid))
    error_exit("You need to be root to change '%s' password\n", name);

  pass = pw->pw_passwd;
  if(pw->pw_passwd[0] == 'x') {
    /*get shadow passwd */
    sp = getspnam(name);
    if(sp)
      pass = sp->sp_pwdp;
  }


  if(!(toys.optflags & (FLAG_l | FLAG_u | FLAG_d))) {
    printf("Changing password for %s\n",name);
    if(pass[0] == '!')
      error_exit("Can't change, password is locked for %s",name);
    if(myuid != 0) {
      /*Validate user */

      if(read_password(toybuf, sizeof(toybuf), "Origial password:")) {
        if(!toys.optargs[0]) free(name);
        return;
      }
      orig = toybuf;
      if(verify_passwd(pass))
        error_exit("Authentication failed\n");
    }

    orig = xstrdup(orig);

    /*Get new password */
    newp = new_password(orig, name);
    if(!newp) {
      free(orig);
      if(!toys.optargs[0]) free(name);
      return; //new password is not set well.
    }

    /*Encrypt the passwd */
    if(!(toys.optflags & FLAG_a)) TT.algo = "des";

    if(get_salt(salt))
      error_exit("Error: Unkown encryption algorithm\n");

    encrypted = crypt(newp, salt);
    free(newp);
    free(orig);
  }
  else if(toys.optflags & FLAG_l) {
    if(pass[0] == '!')
      error_exit("password is already locked for %s",name);
    printf("Locking password for %s\n",name);
    encrypted = xmsprintf("!%s",pass);
  }
  else if(toys.optflags & FLAG_u) {
    if(pass[0] != '!')
      error_exit("password is already unlocked for %s",name);

    printf("Unlocking password for %s\n",name);
    encrypted = xstrdup(&pass[1]);
  }
  else if(toys.optflags & FLAG_d) {
    printf("Deleting password for %s\n",name);
    encrypted = (char*)xzalloc(sizeof(char)*2); //1 = "", 2 = '\0'
  }

  /*Update the passwd */
  if(pw->pw_passwd[0] == 'x')
    ret = update_password("/etc/shadow", name, encrypted);
  else
    ret = update_password("/etc/passwd", name, encrypted);

  if((toys.optflags & (FLAG_l | FLAG_u | FLAG_d)))
    free(encrypted);

  if(!toys.optargs[0]) free(name);
  if(!ret)
    error_msg("Success");
  else
    error_msg("Failure");
}
