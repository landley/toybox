/* vi: set sw=4 ts=4:
 *
 * df.c - report free disk space.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/df.html

USE_DF(NEWTOY(df, "Pkt*a", TOYFLAG_USR|TOYFLAG_SBIN))

config DF
	bool "df (disk free)"
	default y
	help
	  usage: df [-t type] [FILESYSTEM ...]

	  The "disk free" command, df shows total/used/available disk space for
	  each filesystem listed on the command line, or all currently mounted
	  filesystems.

	  -t type
		Display only filesystems of this type.

config DF_PEDANTIC
	bool "options -P and -k"
	default y
	depends on DF
	help
	  usage: df [-Pk]

	  -P	The SUSv3 "Pedantic" option

		Provides a slightly less useful output format dictated by
		the Single Unix Specification version 3, and sets the
		units to 512 bytes instead of the default 1024 bytes.

	  -k	Sets units back to 1024 bytes (the default without -P)
*/

#include "toys.h"

DEFINE_GLOBALS(
	struct arg_list *fstype;

	long units;
)

#define TT this.df

static void show_mt(struct mtab_list *mt)
{
	int len;
	long size, used, avail, percent;
	uint64_t block;

	// Return if it wasn't found (should never happen, but with /etc/mtab...)
	if (!mt) return;

	// If we have -t, skip other filesystem types
	if (TT.fstype) {
		struct arg_list *al;

		for (al = TT.fstype; al; al = al->next) {
			if (!strcmp(mt->type, al->arg)) break;
		}
		if (!al) return;
	}

	// If we don't have -a, skip synthetic filesystems
	if (!(toys.optflags & 1) && !mt->statvfs.f_blocks) return;

	// Figure out how much total/used/free space this filesystem has,
	// forcing 64-bit math because filesystems are big now.
	block = mt->statvfs.f_bsize ? : 1;
	size = (long)((block * mt->statvfs.f_blocks) / TT.units);
	used = (long)((block * (mt->statvfs.f_blocks-mt->statvfs.f_bfree))
			/ TT.units);
	avail = (long)((block
				* (getuid() ? mt->statvfs.f_bavail : mt->statvfs.f_bfree))
			/ TT.units);
	percent = size ? 100-(long)((100*(uint64_t)avail)/size) : 0;

	// Figure out appropriate spacing
	len = 25 - strlen(mt->device);
	if (len < 1) len = 1;
	if (CFG_DF_PEDANTIC && (toys.optflags & 8)) {
		printf("%s %ld %ld %ld %ld%% %s\n", mt->device, size, used, avail,
				percent, mt->dir);
	} else {
		printf("%s% *ld % 10ld % 9ld % 3ld%% %s\n",mt->device, len,
			size, used, avail, percent, mt->dir);
	}
}

void df_main(void)
{
	struct mtab_list *mt, *mt2, *mtlist;

	// Handle -P and -k
	TT.units = 1024;
	if (CFG_DF_PEDANTIC && (toys.optflags & 8)) {
		// Units are 512 bytes if you select "pedantic" without "kilobytes".
		if ((toys.optflags&3) == 1) TT.units = 512;
		printf("Filesystem %ld-blocks Used Available Capacity Mounted on\n",
			TT.units);
	} else puts("Filesystem\t1K-blocks\tUsed Available Use% Mounted on");

	mtlist = getmountlist(1);

	// If we have a list of filesystems on the command line, loop through them.
	if (*toys.optargs) {
		char **next;

		for(next = toys.optargs; *next; next++) {
			struct stat st;

			// Stat it (complain if we can't).
			if(stat(*next, &st)) {
				perror_msg("`%s'", *next);
				toys.exitval = 1;
				continue;
			}

			// Find and display this filesystem.  Use _last_ hit in case of
			// -- bind mounts.
			mt2 = NULL;
			for (mt = mtlist; mt; mt = mt->next)
				if (st.st_dev == mt->stat.st_dev) mt2 = mt;
			show_mt(mt2);
		}
	} else {
		// Get and loop through mount list.

		for (mt = mtlist; mt; mt = mt->next) {
			struct mtab_list *mt2, *mt3;

			if (!mt->stat.st_dev) continue;

			// Filter out overmounts.
			mt3 = mt;
			for (mt2 = mt->next; mt2; mt2 = mt2->next) {
				if (mt->stat.st_dev == mt2->stat.st_dev) {
					// For --bind mounts, take last match
					if (!strcmp(mt->device, mt2->device)) mt3 = mt2;
					// Filter out overmounts
					mt2->stat.st_dev = 0;
				}
			}
			show_mt(mt3);
		}
	}

	if (CFG_TOYBOX_FREE) llist_free(mtlist, NULL);
}
