/* vi: set sw=4 ts=4: */
/*
 * df.c - report free disk space.
 *
 * Implemented according to SUSv3:
 * http://www.opengroup.org/onlinepubs/009695399/utilities/df.html
 * 
 * usage: df [-k] [-P|-t] [file...]
 */

#include "toys.h"

int df_main(void)
{
	struct mtab_list *mt, *mtlist;

	//int units = 512;
	mtlist = getmountlist(1);
	// Zap overmounts
	for (mt = mtlist; mt; mt = mt->next) {
		printf("type=%s dir=%s device=%s\n",mt->type,mt->dir,mt->device);
	}

	return 0;
}
