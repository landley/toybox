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
	int i;
	printf("uid= %d(%s) gid= %d(%s)", pw->pw_uid, pw->pw_name,
									  grp->gr_gid, grp->gr_name);
	if (n) {
		printf(" groups= ");
	}
	for (i = 0; i < n; i++) {
		printf("%d(%s)%s", grps[i]->gr_gid, grps[i]->gr_name,
						   (i < n-1) ? ",": "");
	}
	printf("\n");
}

void id_main(void)
{
	int flags = toys.optflags;

	struct passwd *pw;
	struct group *grp;
	struct group **grps;
	uid_t uid;
	gid_t gid;
	gid_t *groups;
	int i;
	int ngroups;
	char *username;

	/* check if a username is given */
	if (*toys.optargs) {
		username = *(toys.optargs);
		pw = getpwnam(username);
		if (!pw) {
			printf("id: %s: no such user\n", username);
			toys.exitval = 1;
			return;
		}
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

	pw = getpwuid(uid);
	if (!pw) {
		perror("id");
		toys.exitval = 1;
		return;
	}
	
	grp = getgrgid(pw->pw_gid);
	if (!grp) {
		perror("id");
		toys.exitval = 1;
		return;
	}
	
	if (flags & FLAG_u) {
		if (flags & FLAG_n)
		    printf("%s\n", pw->pw_name);
		else
		    printf("%d\n", pw->pw_uid);
		return;
	}
	if (flags & FLAG_g) {
		if (flags & FLAG_n)
			printf("%s\n", grp->gr_name);
		else
			printf("%d\n", grp->gr_gid);
		return;
	}
	

	if (flags & FLAG_r) {
		printf("-r can only be used in combination with -u or -g\n");
		toys.exitval = 1;
		return;
	}
	ngroups = sysconf(_SC_NGROUPS_MAX);
	/* fallback for number of groups to 32 */
	if (ngroups < 0)
		ngroups = 32;
	groups = malloc(ngroups * sizeof(gid_t));
	if (!groups) {
		perror("id");
		toys.exitval = 1;
		return;
	}
	if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) < 0) {
		perror("id");
		toys.exitval = 1;
		return;
	}
	grps = malloc(ngroups * sizeof(struct group *));
	for (i = 0; i < ngroups; i++) {
		struct group *tmp;
		grps[i] = malloc(sizeof(struct group));
		size_t f = sysconf(_SC_GETGR_R_SIZE_MAX);
		char *buf = malloc(f);
		if (getgrgid_r(groups[i], grps[i], buf, f, &tmp) < 0) {
			perror("id");
			continue;
		}
		if (tmp == NULL) {
			perror("id");
			continue;
		}
	}
	
	if (flags & FLAG_G) {
		for (i = 0; i < ngroups; i++) {
			if (flags & FLAG_n)
				printf("%s%s", !i ? "" : " ", grps[i]->gr_name);
			else
				printf("%s%d", !i ? "" : " ", grps[i]->gr_gid);
		}
		printf("\n");
		return;
	}

	pretty_print(pw, grp, grps, ngroups);
	for (i=0; i < ngroups; i++)
		free(grps[i]);

}
