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
    usage: groupdel GROUP

    Delete a group or remove a user from a group
*/

#define FOR_groupdel
#include "toys.h"

char *comma_find(char *name, char *list)
{
  int len = strlen(name);

  while (*list) {
    while (*list == ',') list++;
    if (!strncmp(name, list, len) && (!list[len] || list[len]==','))
      return list;
    while (*list && *list!=',') list++;
  }

  return 0;
}

void groupdel_main(void)
{
  struct group *grp = getgrnam(toys.optargs[toys.optc-1]);
  char *entry = 0;

  if (!grp) perror_exit("group '%s'", toys.optargs[toys.optc-1]);

  // delete user from group
  if (toys.optc == 2) {
    int i, len = 0, found = -1;
    char *s;

    xgetpwnam(*toys.optargs);
    if (grp->gr_mem) {
      for (i = 0; grp->gr_mem[i]; i++) {
        if (found == -1 && !strcmp(*toys.optargs, grp->gr_mem[i])) found = i;
        else len += strlen(grp->gr_mem[i]) + 1;
      }
    }
    if (found == -1)
      error_exit("user '%s' not in group '%s'", *toys.optargs, toys.optargs[1]);

    entry = s = xmalloc(len);
    for (i = 0; grp->gr_mem[i]; ) {
      if (i) *(s++) = ',';
      s = stpcpy(s, grp->gr_mem[i]);
    }

  // delete group
  } else {
    struct passwd *pw;

    endpwent(); // possibly this should be in toy_init()?
    for (;;) {
      if (!(pw = getpwent())) break;
      if (pw->pw_gid == grp->gr_gid) break;
    }
    if (pw) error_exit("can't remove primary group of user '%s'", pw->pw_name);
    endpwent();
  }

  update_password("/etc/group", grp->gr_name, entry);
  update_password("/etc/gshadow", grp->gr_name, entry);
  if (CFG_TOYBOX_FREE) free(entry);
}
