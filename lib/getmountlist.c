/* vi: set sw=4 ts=4 : */
/* getmountlist.c - Get a linked list of mount points, with stat information.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

#include <mntent.h>

char *path_mounts = "/proc/mounts";

// Get a list of mount points from /etc/mtab or /proc/mounts, including
// statvfs() information.  This returns a reversed list, which is good for
// finding overmounts and such.

struct mtab_list *getmountlist(int die)
{
	FILE *fp;
	struct mtab_list *mtlist, *mt;
	struct mntent me;
	char evilbuf[2*PATH_MAX];

	mtlist = 0;
	if (!(fp = setmntent(path_mounts, "r"))) {
		if (die) error_exit("cannot open %s", path_mounts);
	} else {
		while (getmntent_r(fp, &me, evilbuf, sizeof(evilbuf))) {
			mt = xzalloc(sizeof(struct mtab_list) + strlen(me.mnt_fsname) +
				strlen(me.mnt_dir) + strlen(me.mnt_type) + 3);
			mt->next = mtlist;
			// Get information about this filesystem.  Yes, we need both.
			stat(me.mnt_dir, &(mt->stat));
			statvfs(me.mnt_dir, &(mt->statvfs));
			// Remember information from /proc/mounts
			strcpy(mt->type, me.mnt_type);
			mt->dir = mt->type + strlen(mt->type) + 1;
			strcpy(mt->dir, me.mnt_dir);
			mt->device = mt->dir + strlen(mt->dir) + 1;
			strcpy(mt->device, me.mnt_fsname);
			mtlist = mt;
		}
	}
	return mtlist;
}
