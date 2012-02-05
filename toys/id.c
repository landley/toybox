/* vi: set sw=4 ts=4:
 *
 * id.c - print real and effective user and group IDs
 *
 * Copyright 2012 Sony Network Entertainment, Inc.
 *
 * by Tim Bird <tim.bird@am.sony.com>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/id.html

USE_ID(NEWTOY(id, "gru", TOYFLAG_BIN))

config ID
	bool "id"
	default n
	help
	  usage: id [-gru]

	  Print user and group ID.

	  -g    Show only the effective group ID
	  -r	Show real ID instead of effective ID
	  -u    Show only the effective user ID
*/

#include "toys.h"

#define FLAG_g (1<<2)
#define FLAG_r (1<<1)
#define FLAG_u 1

void id_main(void)
{
	int flags = toys.optflags;

	uid_t uid;
	gid_t gid;

	/* show effective, unless user specifies real */
	uid = geteuid();
	gid = getegid();

	if (flags & FLAG_r) {
		uid = getuid();
		gid = getgid();
	}
	if (flags & FLAG_u) {
	    printf("%d\n", uid);
		return;
	}
	if (flags & FLAG_g) {
		printf("%d\n", gid);
		return;
	}
	printf("%d %d\n", uid, gid);
}
