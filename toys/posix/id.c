/* id.c - print real and effective user and group IDs
 *
 * Copyright 2012 Sony Network Entertainment, Inc.
 *
 * by Tim Bird <tim.bird@am.sony.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/id.html

USE_ID(NEWTOY(id, ">1"USE_ID_Z("Z")"nGgru[!"USE_ID_Z("Z")"Ggu]", TOYFLAG_USR|TOYFLAG_BIN))
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

config ID_Z
  bool
  default y
  depends on ID && !TOYBOX_LSM_NONE
  help
    usage: id [-Z]

    -Z	Show only security context

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
#define FORCE_FLAGS
#include "toys.h"

GLOBALS(
  int is_groups;
)

static void s_or_u(char *s, unsigned u, int done)
{
  if (toys.optflags&FLAG_n) printf("%s", s);
  else printf("%u", u);
  if (done) {
    xputc('\n');
    xexit();
  }
}

static void showid(char *header, unsigned u, char *s)
{
  printf("%s%u(%s)", header, u, s);
}

static void do_id(char *username)
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
  if (toys.optflags&FLAG_u) s_or_u(pw->pw_name, pw->pw_uid, 1);

  grp = xgetgrgid(i ? gid : egid);
  if (flags & FLAG_g) s_or_u(grp->gr_name, grp->gr_gid, 1);

  if (!(toys.optflags&(FLAG_G|FLAG_g|FLAG_Z))) {
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

  if (!(toys.optflags&FLAG_Z)) {
    groups = (gid_t *)toybuf;
    i = sizeof(toybuf)/sizeof(gid_t);
    ngroups = username ? getgrouplist(username, gid, groups, &i)
      : getgroups(i, groups);
    if (ngroups<0) perror_exit(0);

    int show_separator = !(toys.optflags&FLAG_G);
    for (i = 0; i<ngroups; i++) {
      if (show_separator) xputc((toys.optflags&FLAG_G) ? ' ' : ',');
      show_separator = 1;
      if (!(grp = getgrgid(groups[i]))) perror_msg(0);
      else if (toys.optflags&FLAG_G) s_or_u(grp->gr_name, grp->gr_gid, 0);
      else if (grp->gr_gid != egid) showid("", grp->gr_gid, grp->gr_name);
      else show_separator = 0; // Because we didn't show anything this time.
    }
    if (toys.optflags&FLAG_G) {
      xputc('\n');
      xexit();
    }
  }

  if (!CFG_TOYBOX_LSM_NONE) {
    if (lsm_enabled()) {
      char *context = lsm_context();

      printf(" context=%s"+!!(toys.optflags&FLAG_Z), context);
      if (CFG_TOYBOX_FREE) free(context);
    } else if (toys.optflags&FLAG_Z) error_exit("%s disabled", lsm_name());
  }

  xputc('\n');
}

void id_main(void)
{
  if (toys.optc) while(*toys.optargs) do_id(*toys.optargs++);
  else do_id(NULL);
}

void groups_main(void)
{
  TT.is_groups = 1;
  toys.optflags = FLAG_G|FLAG_n;
  id_main();
}

void logname_main(void)
{
  toys.optflags = FLAG_u|FLAG_n;
  id_main();
}
