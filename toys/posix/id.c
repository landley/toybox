/* id.c - print real and effective user and group IDs
 *
 * Copyright 2012 Sony Network Entertainment, Inc.
 *
 * by Tim Bird <tim.bird@am.sony.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/id.html

USE_ID(NEWTOY(id, ">1"USE_ID_SELINUX("Z")"nGgru[!"USE_ID_SELINUX("Z")"Ggu]", TOYFLAG_USR|TOYFLAG_BIN))
USE_GROUPS(NEWTOY(groups, NULL, TOYFLAG_USR|TOYFLAG_BIN))
USE_LOGNAME(NEWTOY(logname, ">0", TOYFLAG_USR|TOYFLAG_BIN))
USE_WHOAMI(OLDTOY(whoami, logname, TOYFLAG_USR|TOYFLAG_BIN))

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

config ID_SELINUX
  bool
  default y
  depends on ID && TOYBOX_SELINUX
  help
    usage: id [-Z]

    -Z Show only SELinux context

config GROUPS
  bool "groups"
  default y
  help
    usage: groups [user]

    Print the groups a user is in.

config LOGNAME
  bool "logname"
  default y
  help
    usage: logname

    Print the current user name.

config WHOAMI
  bool "whoami"
  default y
  help
    usage: whoami

    Print the current user name.
*/

#define FOR_id
#include "toys.h"

GLOBALS(
  int do_u, do_n, do_G, do_Z, is_groups;
)

static void s_or_u(char *s, unsigned u, int done)
{
  if (TT.do_n) printf("%s", s);
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
  int flags, i, ngroups;
  struct passwd *pw;
  struct group *grp;
  uid_t uid = getuid(), euid = geteuid();
  gid_t gid = getgid(), egid = getegid(), *groups;

  flags = toys.optflags;

  // check if a username is given
  if (username) {
    pw = xgetpwnam(username);
    uid = euid = pw->pw_uid;
    gid = egid = pw->pw_gid;
    if (TT.is_groups) printf("%s : ", pw->pw_name);
  }

  i = flags & FLAG_r;
  pw = xgetpwuid(i ? uid : euid);
  if (TT.do_u) s_or_u(pw->pw_name, pw->pw_uid, 1);

  grp = xgetgrgid(i ? gid : egid);
  if (flags & FLAG_g) s_or_u(grp->gr_name, grp->gr_gid, 1);

  if (!TT.do_G && !TT.do_Z) {
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

  if (!TT.do_Z) {
    groups = (gid_t *)toybuf;
    i = sizeof(toybuf)/sizeof(gid_t);
    ngroups = username ? getgrouplist(username, gid, groups, &i)
      : getgroups(i, groups);
    if (ngroups<0) perror_exit(0);

    int show_separator = !TT.do_G;
    for (i = 0; i<ngroups; i++) {
      if (show_separator) xputc(TT.do_G ? ' ' : ',');
      show_separator = 1;
      if (!(grp = getgrgid(groups[i]))) perror_msg(0);
      else if (TT.do_G) s_or_u(grp->gr_name, grp->gr_gid, 0);
      else if (grp->gr_gid != egid) showid("", grp->gr_gid, grp->gr_name);
      else show_separator = 0; // Because we didn't show anything this time.
    }
    if (TT.do_G) {
      xputc('\n');
      exit(0);
    }
  }

  if (CFG_TOYBOX_SELINUX) {
    char *context = NULL;

    if (is_selinux_enabled() < 1) {
      if (TT.do_Z)
        error_exit("SELinux disabled");
    } else if (getcon(&context) == 0) {
      if (!TT.do_Z) xputc(' ');
      printf("context=%s", context);
    }
    if (CFG_TOYBOX_FREE) free(context);
  }

  xputc('\n');
}

void id_main(void)
{
  // FLAG macros can be 0 if "id" command not enabled, so snapshot them here.
  if (FLAG_u) TT.do_u |= toys.optflags & FLAG_u;
  if (FLAG_n) TT.do_n |= toys.optflags & FLAG_n;
  if (FLAG_G) TT.do_G |= toys.optflags & FLAG_G;
  if (FLAG_Z) TT.do_Z |= toys.optflags & FLAG_Z;

  if (toys.optc) while(*toys.optargs) do_id(*toys.optargs++);
  else do_id(NULL);
}

void groups_main(void)
{
  TT.is_groups = 1;
  TT.do_G = TT.do_n = 1;
  id_main();
}

void logname_main(void)
{
  TT.do_u = TT.do_n = 1;
  id_main();
}
