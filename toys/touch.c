/* vi: set sw=4 ts=4:
 *
 * touch.c - Modify a file's timestamps.
 *
 * Copyright (C) 2007 Charlie Shepherd <masterdriverz@gentoo.org>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/touch.html

USE_TOUCH(NEWTOY(touch, "l#t:r:mca", TOYFLAG_BIN|TOYFLAG_UMASK))

config TOUCH
	bool "touch"
	default y
	help
	  usage: touch [-acm] [-r FILE] [-t MMDDhhmm] [-l bytes] FILE...

	  Change file timestamps, ensure file existance and change file length.

	  -a	Only change the access time.
	  -c	Do not create the file if it doesn't exist.
	  -l	Length to truncate (or sparsely extend) file to.
	  -m	Only change the modification time.
	  -r	Reference file to take timestamps from.
	  -t	Time to change {a,m}time to.
*/

#include "toys.h"

DEFINE_GLOBALS(
	char *ref_file;
	char *time;
	long length;
)

#define TT this.touch

#define OPT_MTIME       0x01
#define OPT_NOCREATE    0x02
#define OPT_ATIME       0x04
#define OPT_REFERENCE   0x08
#define OPT_TIME        0x10
#define OPT_LENGTH      0x20

void touch_main(void)
{
	char *arg;
	int i, set_a, set_m;
	time_t curr_a, curr_m;

	set_a = !!(toys.optflags & OPT_ATIME);
	set_m = !!(toys.optflags & OPT_MTIME);

	// Use timestamp on a file
	if (toys.optflags & OPT_REFERENCE) {
		struct stat sb;

		if (toys.optflags & OPT_TIME)
			error_exit("Redundant time source");
		xstat(TT.ref_file, &sb);
		curr_m = sb.st_mtime;
		curr_a = sb.st_atime;

	// Use time specified on command line.
	} else if (toys.optflags & OPT_TIME) {
		struct tm t;
		time_t curr;
		char *c;

		curr = time(NULL);
		if (localtime_r(&curr, &t)
			|| !(c = strptime(TT.time, "%m%d%H%M", &t))
			|| *c || -1==(curr_a = curr_m = mktime(&t)))
		{
			error_exit("Unknown time %s", TT.time);
		}

	// use current time
	} else curr_m = curr_a = time(NULL);

	for (i = 0; (arg = toys.optargs[i]); i++) {
		struct utimbuf buf;
		struct stat sb;

		buf.modtime = curr_m;
		buf.actime = curr_a;

		if (stat(arg, &sb)) {
			if (!(toys.optflags & OPT_NOCREATE)) {
				xcreate(arg, O_CREAT, 0644);
				if (stat(arg, &sb))
					goto error;
			}
		}

		if ((set_a+set_m) == 1) {
			/* We've been asked to only change one */
			if (set_a) buf.modtime = sb.st_mtime;
			else if (set_m) buf.actime = sb.st_atime;
		}

		if (toys.optflags & OPT_LENGTH)
			if (truncate(arg, TT.length))
				goto error;
		if (utime(arg, &buf))
error:
			perror_exit(arg);
	}
}
