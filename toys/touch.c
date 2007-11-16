/* vi: set sw=4 ts=4: */
/*
 * touch.c - Modify a file's timestamps.
 *
 * Copyright (C) 2007 Charlie Shepherd <masterdriverz@gentoo.org>
 */

#define _XOPEN_SOURCE 600
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <time.h>
#include "toys.h"

#define MTIME		0x01
#define NO_CREATE	0x02
#define ATIME		0x04
#define REFERENCE	0x08
#define TIME		0x10
#define LENGTH		0x20

int touch_main(void)
{
	char *arg;
	int i, set_a, set_m, create;
	time_t curr_a, curr_m;

	set_a = !!(toys.optflags & ATIME);
	set_m = !!(toys.optflags & MTIME);
	create = !(toys.optflags & NO_CREATE);

	if (toys.optflags & REFERENCE) {
		struct stat sb;
		if (toys.optflags & TIME)
			error_exit("Cannot specify times from more than one source");
		xstat(toy.touch.ref_file, &sb);
		curr_m = sb.st_mtime;
		curr_a = sb.st_atime;
	} else if (toys.optflags & TIME) {
		struct tm t;
		time_t curr;
		char *c;
		curr = time(NULL);
		if (!localtime_r(&curr, &t))
			goto time_error;
		c = strptime(toy.touch.time, "%m%d%H%M", &t);
		if (!c || *c)
			goto time_error;
		curr_a = curr_m = mktime(&t);
		if (curr_a == -1)
time_error:
			error_exit("Error converting time %s to internal format",
				toy.touch.time);
	} else {
		curr_m = curr_a = time(NULL);
	}

	for (i = 0; (arg = toys.optargs[i]); i++) {
		struct utimbuf buf;
		struct stat sb;

		buf.modtime = curr_m;
		buf.actime = curr_a;

		if (stat(arg, &sb) == -1) {
			if (create && errno == ENOENT) {
				if (creat(arg, 0644))
					goto error;
				if (stat(arg, &sb))
					goto error;
			}
		}

		if ((set_a+set_m) == 1) {
			/* We've been asked to only change one */
			if (set_a)
				buf.modtime = sb.st_mtime;
			else if (set_m)
				buf.actime = sb.st_atime;
		}

		if (toys.optflags & LENGTH)
			if (truncate(arg, toy.touch.length))
				goto error;
		if (utime(arg, &buf))
error:
			perror_exit(arg);
	}

	return 0;
}
