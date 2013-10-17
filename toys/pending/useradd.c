/* useradd.c - add a new user
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/useradd.html

USE_USERADD(NEWTOY(useradd, "<1>2u#<0G:s:g:h:SDH", TOYFLAG_NEEDROOT|TOYFLAG_SBIN))
USE_USERADD(OLDTOY(adduser, useradd, OPTSTR_useradd, TOYFLAG_NEEDROOT|TOYFLAG_SBIN))

config USERADD
  bool "useradd"
  default n
  help
    usage: useradd [-SDH] [-hDIR] [-sSHELL] [-G GRP] [-gGECOS] [-uUID] USER [GROUP]

    Create new user, or add USER to GROUP
    
    -h DIR   Home directory
    -g GECOS GECOS field
    -s SHELL Login shell
    -G GRP   Add user to existing group
    -S       Create a system user
    -D       Don't assign a password
    -H       Don't create home directory
    -u UID   User id
*/

#define FOR_useradd
#include "toys.h"

GLOBALS(
  char *dir;
  char *gecos;
  char *shell;
  char *u_grp;
  long uid;
  long gid;
)

static char* get_shell(void)    
{                               
  char *shell = getenv("SHELL");

  if (!shell) {
    struct passwd *pw;
    pw = getpwuid(getuid());
    if (pw && pw->pw_shell && pw->pw_shell[0])
      shell = pw->pw_shell;                                                                                                           
    else shell = "/bin/sh";     
  }                             
  return xstrdup(shell);        
}

/* exec_wait() function does a fork(), and exec the command,
 * waits for the child to exit and return the status to parent
 */
static int exec_wait(char **args)
{
  int status = 0;
  pid_t pid = fork();

  if (!pid) xexec(args);
  else if (pid > 0) waitpid(pid, &status, 0);
  else perror_exit("fork failed");
  return WEXITSTATUS(status);
}

/* create_copy_skel(), This function will create the home directory of the
 * user, by copying /etc/skel/ contents to /home/<username>.
 * Then change the ownership of home dir to the UID and GID of new user,
 * and Mode to 0700, i.e. rwx------ for user.
 */
static void create_copy_skel(char *skel, char *hdir)
{
  char *args[5];
  struct stat sb;

  if (toys.optflags & FLAG_H) return;

  umask(0);
  args[4] = NULL;
  if (stat(hdir, &sb)) {
    args[0] = "cp";
    args[1] = "-R";
    args[2] = skel;
    args[3] = hdir;
    // Copy /etc/skel to home dir 
    toys.exitval = exec_wait(args);

    args[0] = "chown";
    args[1] = "-R";
    args[2] = xmsprintf("%u:%u", TT.uid, TT.gid);
    args[3] = hdir;
    //Change ownership to that of UID and GID of new user
    toys.exitval = exec_wait(args);

  } else xprintf("Warning: home directory for the user already exists\n"
      "Not copying any file from skel directory into it.\n");

  if (chown(hdir, TT.uid, TT.gid) || chmod(hdir, 0700))
    perror_exit("chown/chmod failed for '%s'", hdir);
}

/* Add a new group to the system, if UID is given then that is validated
 * to be free, else a free UID is choosen by self.
 * SYSTEM IDs are considered in the range 100 ... 999
 * add_user(), add a new entry in /etc/passwd, /etc/shadow files
 */
static void new_user()
{
  struct passwd pwd;
  char *entry, *args[4];
  int max = INT_MAX;

  pwd.pw_name = *toys.optargs;
  pwd.pw_passwd = (char *)"x";
  if (toys.optflags & FLAG_g) pwd.pw_gecos = TT.gecos;
  else pwd.pw_gecos = "Linux User,";
  if (toys.optflags & FLAG_h) pwd.pw_dir = TT.dir;
  else pwd.pw_dir = xmsprintf("/home/%s", *toys.optargs);
  if (toys.optflags & FLAG_s) pwd.pw_shell = TT.shell;
  else pwd.pw_shell = get_shell();

  if (toys.optflags & FLAG_u) {
    if (TT.uid > INT_MAX) error_exit("uid should be less than  '%d' ", INT_MAX);
    if (getpwuid(TT.uid)) error_exit("user '%ld' is in use", TT.uid);
    pwd.pw_uid = TT.uid;
  } else {
    if (toys.optflags & FLAG_S) {
      TT.uid = SYS_FIRST_ID;
      max = SYS_LAST_ID;
    } else {
      TT.uid = SYS_LAST_ID + 1; //i.e. starting from 1000
      max = 60000; // as per config file on Linux desktop
    }
    //find unused uid
    while (TT.uid <= max) {
      if (!getpwuid(TT.uid)) break;
      if (TT.uid == max) error_exit("no more free uids left");
      TT.uid++;
    }
    pwd.pw_uid = TT.uid;
  }

  if (toys.optflags & FLAG_G) {
    struct group *gr = getgrnam(TT.u_grp);
    if (!gr) error_exit("The group '%s' doesn't exist", TT.u_grp);
    TT.gid = gr->gr_gid;
  } else {
    // Set the GID for the user, if not specified
    if (toys.optflags & FLAG_S) {
      TT.gid = SYS_FIRST_ID;
      max = SYS_LAST_ID;
    } else TT.gid = ((TT.uid > SYS_LAST_ID) ? TT.uid : SYS_LAST_ID + 1);
    if (getgrnam(pwd.pw_name)) error_exit("group '%s' is in use", pwd.pw_name);
    //find unused gid
    while (TT.gid <= max) {
      if (!getgrgid(TT.gid)) break;
      if (TT.gid == max) error_exit("no more free gids left");
      TT.gid++;   
    }
  }
  pwd.pw_gid = TT.gid;

  if (!(toys.optflags & FLAG_G)) {
    // Create a new group for user 
    //add group, invoke addgroup command
    args[0] = "groupadd";
    args[1] = toys.optargs[0];
    args[2] = xmsprintf("-g%ld", pwd.pw_gid);
    args[3] = NULL;
    if (exec_wait(args)) error_msg("addgroup fail");
  }

  /*add user to system 
   * 1. add an entry to /etc/passwd and /etcshadow file
   * 2. Copy /etc/skel dir contents to use home dir
   * 3. update the user passwd by running 'passwd' utility
   */

  // 1. add an entry to /etc/passwd and /etc/shadow file
  entry = xmsprintf("%s:%s:%ld:%ld:%s:%s:%s", pwd.pw_name, pwd.pw_passwd,
      pwd.pw_uid, pwd.pw_gid, pwd.pw_gecos, pwd.pw_dir, pwd.pw_shell);
  if (update_password("/etc/passwd", pwd.pw_name, entry)) error_exit("updating passwd file failed");
  free(entry);

  if (toys.optflags & FLAG_S) 
  entry = xmsprintf("%s:!!:%u::::::", pwd.pw_name, 
      (unsigned)(time(NULL))/(24*60*60)); //passwd is not set initially
  else entry = xmsprintf("%s:!!:%u:%ld:%ld:%ld:::", pwd.pw_name, 
            (unsigned)(time(NULL))/(24*60*60), 0, 99999, 7); //passwd is not set initially
  update_password("/etc/shadow", pwd.pw_name, entry);
  free(entry);

  //2. craete home dir & copy skel dir to home
  if (!(toys.optflags & FLAG_S)) create_copy_skel("/etc/skel", pwd.pw_dir);

  //3. update the user passwd by running 'passwd' utility
  if (!(toys.optflags & FLAG_D)) {
    args[0] = "passwd";
    args[1] = pwd.pw_name;
    args[2] = NULL;
    if (exec_wait(args)) error_exit("changing user passwd failed");
  }
  if (toys.optflags & FLAG_G) {
    /*add user to the existing group, invoke addgroup command */
    args[0] = "groupadd";
    args[1] = toys.optargs[0];
    args[2] = TT.u_grp;
    args[3] = NULL;
    if (exec_wait(args)) error_exit("adding user to group Failed");
  }
}

/* Entry point for useradd feature 
 * Specifying options and User, Group at cmdline is treated as error.
 * If only 2 parameters (Non-Option) are given, then User is added to the 
 * Group
 */
void useradd_main(void)
{
  struct passwd *pwd = NULL;

  if (toys.optflags && toys.optc == 2) {
    toys.exithelp = 1;
    error_exit("options, user and group can't be together");
  }

  if (toys.optc == 2) {
    //add user to group
    //toys.optargs[0]- user, toys.optargs[1] - group
    char *args[4];
    args[0] = "groupadd";
    args[1] = toys.optargs[0];
    args[2] = toys.optargs[1];
    args[3] = NULL;
    toys.exitval = exec_wait(args);
  } else {    //new user to be created
    // investigate the user to be created
    if ((pwd = getpwnam(*toys.optargs))) 
      error_exit("user '%s' is in use", *toys.optargs);
    is_valid_username(*toys.optargs); //validate the user name
    new_user();
  }
}
