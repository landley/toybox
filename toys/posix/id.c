/* id.c - print real and effective user and group IDs
 *
 * Copyright 2012 Sony Network Entertainment, Inc.
 *
 * by Tim Bird <tim.bird@am.sony.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/id.html

USE_ID(NEWTOY(id, ">1nGgru[!Ggu]", TOYFLAG_BIN))
USE_ID_GROUPS(OLDTOY(groups, id, NULL, TOYFLAG_USR|TOYFLAG_BIN))
USE_ID_LOGNAME(OLDTOY(logname, id, ">0", TOYFLAG_BIN))
USE_ID_LOGNAME(OLDTOY(whoami, id, ">0", TOYFLAG_BIN))

config ID
  bool "id"
  default y
  help
    usage: id [-nGgru]

    Print user and group ID.

    -n	print names instead of numeric IDs (to be used with -Ggu)
    -G	Show only the group IDs
    -g	Show only the effective group ID
    -r	Show real ID instead of effective ID
    -u	Show only the effective user ID

config ID_GROUPS
  bool "groups"
  default y
  depends on ID
  help
    usage: groups [user]

    Print the groups a user is in.

config ID_LOGNAME
  bool "logname"
  default y
  depends on ID
  help
    usage: logname

    Print the current user name.

*/

#define FOR_id
#include "toys.h"

static void s_or_u(char *s, unsigned u, int done)
{
  if (toys.optflags & FLAG_n) printf("%s", s);
  else printf("%u", u);
  if (done) {
    xputc('\n');
    exit(0);
  }
}

static void showid(char *header, unsigned u, char *s)
{
  printf("%s%u(%s)", header, u, s);
}

void do_id(char *username)
{
  int flags, i, ngroups, cmd_groups = toys.which->name[0] == 'g';
  struct passwd *pw;
  struct group *grp;
  uid_t uid = getuid(), euid = geteuid();
  gid_t gid = getgid(), egid = getegid(), *groups;

  if (cmd_groups)
      toys.optflags |= FLAG_G | FLAG_n;

  flags = toys.optflags;

  // check if a username is given
  if (username) {
    pw = xgetpwnam(username);
    uid = euid = pw->pw_uid;
    gid = egid = pw->pw_gid;
    if (cmd_groups) printf("%s : ", pw->pw_name);
  }

  i = flags & FLAG_r;
  pw = xgetpwuid(i ? uid : euid);
  if (flags & FLAG_u) s_or_u(pw->pw_name, pw->pw_uid, 1);

  grp = xgetgrgid(i ? gid : egid);
  if (flags & FLAG_g) s_or_u(grp->gr_name, grp->gr_gid, 1);

  if (!(flags & FLAG_G)) {
    showid("uid=", pw->pw_uid, pw->pw_name);
    showid(" gid=", grp->gr_gid, grp->gr_name);

    if (!i) {
      if (uid != euid) {
        pw = xgetpwuid(euid);
        showid(" euid=", pw->pw_uid, pw->pw_name);
      }
      if (gid != egid) {
        grp = xgetgrgid(egid);
        showid(" egid=", grp->gr_gid, grp->gr_name);
      }
    }

    showid(" groups=", grp->gr_gid, grp->gr_name);
  }

  groups = (gid_t *)toybuf;
  i = sizeof(toybuf)/sizeof(gid_t);
  ngroups = username ? getgrouplist(username, gid, groups, &i)
    : getgroups(i, groups);
  if (ngroups<0) perror_exit(0);

  for (i = 0; i<ngroups; i++) {
    if (i) xputc(' ');
    if (!(grp = getgrgid(groups[i]))) perror_msg(0);
    else if (flags & FLAG_G) s_or_u(grp->gr_name, grp->gr_gid, 0);
    else if (grp->gr_gid != egid) showid("", grp->gr_gid, grp->gr_name);
  }
  xputc('\n');
}

void id_main(void)
{
  if (toys.which->name[0] > 'i') toys.optflags = (FLAG_u | FLAG_n);
  if (toys.optc) while(*toys.optargs) do_id(*toys.optargs++);
  else do_id(NULL);
}
