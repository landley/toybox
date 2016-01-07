/* groupadd.c - create a new group
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/groupadd.html

USE_GROUPADD(NEWTOY(groupadd, "<1>2g#<0S", TOYFLAG_NEEDROOT|TOYFLAG_SBIN))
USE_GROUPADD(OLDTOY(addgroup, groupadd, TOYFLAG_NEEDROOT|TOYFLAG_SBIN))

config GROUPADD
  bool "groupadd"
  default n
  help
    usage: groupadd [-S] [-g GID] [USER] GROUP

    Add a group or add a user to a group
    
      -g GID Group id
      -S     Create a system group
*/

#define FOR_groupadd
#include "toys.h"

#define GROUP_PATH        "/etc/group"
#define SECURE_GROUP_PATH "/etc/gshadow"

GLOBALS(
  long gid;
)

/* Add a new group to the system, if GID is given then that is validated
 * to be free, else a free GID is choosen by self.
 * SYSTEM IDs are considered in the range 100 ... 999
 * update_group(), updates the entries in /etc/group, /etc/gshadow files
 */
static void new_group()
{
  char *entry = NULL;

  if (toys.optflags & FLAG_g) {
    if (TT.gid > INT_MAX) error_exit("gid should be less than  '%d' ", INT_MAX);
    if (getgrgid(TT.gid)) error_exit("group '%ld' is in use", TT.gid);
  } else {
    if (toys.optflags & FLAG_S) TT.gid = CFG_TOYBOX_UID_SYS;
    else TT.gid = CFG_TOYBOX_UID_USR;
    //find unused gid
    while (getgrgid(TT.gid)) TT.gid++;
  }

  entry = xmprintf("%s:%s:%d:", *toys.optargs, "x", TT.gid);
  update_password(GROUP_PATH, *toys.optargs, entry);
  free(entry);
  entry = xmprintf("%s:%s::", *toys.optargs, "!");
  update_password(SECURE_GROUP_PATH, *toys.optargs, entry);
  free(entry);
}

void groupadd_main(void)
{
  struct group *grp = NULL;
  char *entry = NULL;

  if (toys.optflags && toys.optc == 2)
    help_exit("options, user and group can't be together");

  if (toys.optc == 2) {  //add user to group
    //toys.optargs[0]- user, toys.optargs[1] - group
    xgetpwnam(*toys.optargs);
    if (!(grp = getgrnam(toys.optargs[1]))) 
      error_exit("group '%s' does not exist", toys.optargs[1]);
    if (!grp->gr_mem) entry = xmprintf("%s", *toys.optargs);
    else {
      int i;

      for (i = 0; grp->gr_mem[i]; i++)
        if (!strcmp(grp->gr_mem[i], *toys.optargs)) return;

      entry = xstrdup("");
      for (i=0; grp->gr_mem[i]; i++) {
        entry = xrealloc(entry, strlen(entry) + strlen(grp->gr_mem[i]) + 2);
        strcat(entry, grp->gr_mem[i]);
        strcat(entry, ",");
      }
      entry = xrealloc(entry, strlen(entry) + strlen(*toys.optargs) + 1);
      strcat(entry, *toys.optargs);
    }
    update_password(GROUP_PATH, grp->gr_name, entry);
    update_password(SECURE_GROUP_PATH, grp->gr_name, entry);
    free(entry);
  } else {    //new group to be created
    char *s = *toys.optargs;

    /* investigate the group to be created */
    if (getgrnam(s)) error_exit("'%s' in use", s);
    if (s[strcspn(s, ":/\n")] || strlen(s) > LOGIN_NAME_MAX)
      error_exit("bad name");
    new_group();
  }
}
