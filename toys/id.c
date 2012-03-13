/* vi: set sw=4 ts=4:
 *
 * id.c - print real and effective user and group IDs
 *
 * Copyright 2012 Sony Network Entertainment, Inc.
 *
 * by Tim Bird <tim.bird@am.sony.com>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/id.html

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

void pretty_print(struct passwd *pw, struct group *grp, struct group **grps,
		int n)
{
	int i = 0;

	printf("uid=%u(%s) gid=%u(%s)", pw->pw_uid, pw->pw_name,
									grp->gr_gid, grp->gr_name);
	if (n) printf(" groups=");
	while (i < n) {
		printf("%d(%s)", grps[i]->gr_gid, grps[i]->gr_name);
		if (++i<n) xputc(',');
	}
	xputc('\n');
}

void id_main(void)
{
	int flags = toys.optflags, i, ngroups;
	struct passwd *pw;
	struct group *grp, **grps;
	uid_t uid;
	gid_t gid, *groups;

	/* check if a username is given */
	if (*toys.optargs) {
		if (!(pw = getpwnam(*toys.optargs)))
			error_exit("no such user '%s'", *toys.optargs);
		uid = pw->pw_uid;
		gid = pw->pw_gid;
	} else {
		/* show effective, unless user specifies real */
		if (flags & FLAG_r) {
			uid = getuid();
			gid = getgid();
		} else {
			uid = geteuid();
			gid = getegid();
		}
	}

	if (!(pw = getpwuid(uid)) || !(grp = getgrgid(gid)))
		perror_exit(0);
	
	if (flags & FLAG_u) {
		if (flags & FLAG_n) xputs(pw->pw_name);
		else printf("%d\n", pw->pw_uid);
		return;
	}
	if (flags & FLAG_g) {
		if (flags & FLAG_n) xputs(grp->gr_name);
		else printf("%d\n", grp->gr_gid);
		return;
	}
	
	ngroups = sysconf(_SC_NGROUPS_MAX);
	if (ngroups<1) ngroups = 32;
	groups = xmalloc(ngroups * sizeof(gid_t));
	if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) < 0)
		perror_exit(0);

	grps = xmalloc(ngroups * sizeof(struct group *));
	for (i = 0; i < ngroups; i++) {
		struct group *tmp;
		grps[i] = xmalloc(sizeof(struct group));
		size_t f = sysconf(_SC_GETGR_R_SIZE_MAX);
		char *buf = xmalloc(f);
		if (getgrgid_r(groups[i], grps[i], buf, f, &tmp) < 0 || !tmp) {
			perror_msg(0);
			continue;
		}
	}
	
	if (flags & FLAG_G) {
		for (i = 0; i < ngroups; i++) {
			if (i) xputc(' ');
			if (flags & FLAG_n) printf("%s", grps[i]->gr_name);
			else printf("%d", grps[i]->gr_gid);
		}
		printf("\n");
		return;
	}

	pretty_print(pw, grp, grps, ngroups);
	for (i=0; i < ngroups; i++)
		free(grps[i]);

}
