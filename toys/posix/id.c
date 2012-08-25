/* vi: set sw=4 ts=4:
 *
 * id.c - print real and effective user and group IDs
 *
 * Copyright 2012 Sony Network Entertainment, Inc.
 *
 * by Tim Bird <tim.bird@am.sony.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/id.html

USE_ID(NEWTOY(id, "nGgru", TOYFLAG_BIN))

config ID
	bool "id"
	default y
	help
	  usage: id [-nGgru]

	  Print user and group ID.

	  -n	print names instead of numeric IDs (to be used with -Ggu)
	  -G	Show only the group IDs
	  -g    Show only the effective group ID
	  -r	Show real ID instead of effective ID
	  -u    Show only the effective user ID
*/

#include "toys.h"

#define FLAG_n (1<<4)
#define FLAG_G (1<<3)
#define FLAG_g (1<<2)
#define FLAG_r (1<<1)
#define FLAG_u 1

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

struct passwd *xgetpwuid(uid_t uid)
{
	struct passwd *pwd = getpwuid(uid);
	if (!pwd) error_exit(NULL);
	return pwd;
}

struct group *xgetgrgid(gid_t gid)
{
	struct group *group = getgrgid(gid);
	if (!group) error_exit(NULL);
	return group;
}

void id_main(void)
{
	int flags = toys.optflags, i, ngroups;
	struct passwd *pw;
	struct group *grp;
	uid_t uid = getuid(), euid = geteuid();
	gid_t gid = getgid(), egid = getegid(), *groups;

	/* check if a username is given */
	if (*toys.optargs) {
		if (!(pw = getpwnam(*toys.optargs)))
			error_exit("no such user '%s'", *toys.optargs);
		uid = euid = pw->pw_uid;
		gid = egid = pw->pw_gid;
	}

	i = toys.optflags & FLAG_r;
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
	if (0 >= (ngroups = getgroups(sizeof(toybuf)/sizeof(gid_t), groups)))
		perror_exit(0);

	for (i = 0; i < ngroups; i++) {
		xputc(' ');
		if (!(grp = getgrgid(groups[i]))) perror_msg(0);
		else if (flags & FLAG_G)
			s_or_u(grp->gr_name, grp->gr_gid, 0);
		else if (grp->gr_gid != egid) showid("", grp->gr_gid, grp->gr_name);
	}
	xputc('\n');
}
