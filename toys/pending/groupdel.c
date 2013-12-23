/* groupdel.c - delete a group
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini1981@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/groupdel.html

USE_GROUPDEL(NEWTOY(groupdel, "<1>2", TOYFLAG_NEEDROOT|TOYFLAG_SBIN))
USE_GROUPDEL(OLDTOY(delgroup, groupdel, OPTSTR_groupdel, TOYFLAG_NEEDROOT|TOYFLAG_SBIN))

config GROUPDEL
  bool "groupdel"
  default n
  help
    usage: delgroup [USER] GROUP
    groupdel GROUP

    Delete a group or delete a user from a group
*/

#define FOR_groupdel
#include "toys.h"

void groupdel_main(void)
{
  struct group *grp = NULL;
  char *entry = NULL;

  if (toys.optc == 2) {  //del user from group
    //toys.optargs[0]- user, toys.optargs[1] - group
    if (!getpwnam(toys.optargs[0])) 
      error_exit("user '%s' does not exist", toys.optargs[0]);
    if (!(grp = getgrnam(toys.optargs[1]))) 
      error_exit("group '%s' does not exist", toys.optargs[1]);
    if (!(grp = getgrnam(toys.optargs[1]))) 
      error_exit("group '%s' does not exist", toys.optargs[1]);
    if (!grp->gr_mem) return;
    else {
      int i, found = -1;

      for (i = 0; grp->gr_mem[i] && (found == -1); i++)
        if (!strcmp(grp->gr_mem[i], *toys.optargs)) found = i;

      if (found == -1) {
        xprintf("%s: The user '%s' is not a member of '%s'\n", toys.which->name,
            toys.optargs[0], toys.optargs[1]);
        return;
      }
      entry = xstrdup("");
      for (i=0; grp->gr_mem[i]; i++) {
        if (found != i) { //leave out user from grp member list
          if (i && *entry) strcat(entry, ",");
          entry = xrealloc(entry, strlen(entry) + strlen(grp->gr_mem[i]) + 2);
          strcat(entry, grp->gr_mem[i]);
        }
      }
    }
  } else {    //delete the group
    struct passwd *pw = NULL;

    if (!(grp = getgrnam(*toys.optargs))) 
      error_exit("group '%s' doesn't exist", *toys.optargs);
    //is it a primary grp of user
    while ((pw = getpwent())) {
      if (pw->pw_gid == grp->gr_gid) {
        endpwent();
        error_exit("can't remove primary group of user '%s'", pw->pw_name);
      }
    }
    endpwent();
  }
  update_password("/etc/group", grp->gr_name, entry);
  update_password("/etc/gshadow", grp->gr_name, entry);
  free(entry);
}
