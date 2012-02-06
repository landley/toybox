/* vi: set sw=4 ts=4:
 *
 * cmp.c - Compare two files.
 *
 * Copyright 2012 Timothy Elliott <tle@holymonkey.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/cmp.html

USE_CMP(NEWTOY(cmp, "<2>2ls", TOYFLAG_USR|TOYFLAG_BIN))

config CMP
	bool "cmp"
	default y
	help
	  usage: cmp [-l] [-s] FILE1 FILE2

	  Compare the contents of two files.

	  -l show all differing bytes
	  -s silent
*/

#include "toys.h"

#define FLAG_s	1
#define FLAG_l	2

int get_fd(char *file)
{
	int fd;

	if (!strcmp(file,"-")) fd=0;
	else if (0>(fd = open(file, O_RDONLY, 0))) {
		perror_exit("%s", file);
	}
	return fd;
}

void do_cmp(int fd1, int fd2, char *file1, char *file2, char *buf1, char *buf2,
	size_t size) {
	int i, len1, len2, min_len;
	size_t byte_no = 1, line_no = 1;

	for (;;) {
		len1 = read(fd1, buf1, size);
		len2 = read(fd2, buf2, size);

		min_len = len1 < len2 ? len1 : len2;
		for (i=0; i<min_len; i++) {
			if (buf1[i] != buf2[i]) {
				toys.exitval = 1;
				if (toys.optflags & FLAG_l) {
					printf("%d %o %o\n", byte_no, buf1[i],
						buf2[i]);
				}
				else {
					if (!(toys.optflags & FLAG_s)) {
						printf("%s %s differ: char %d, line %d\n",
							file1, file2, byte_no,
							line_no);
					}
					return;
				}

			}
			byte_no++;
			if (buf1[i] == '\n') line_no++;
		}
		if (len1 != len2) {
			if (!(toys.optflags & FLAG_s)) {
				fdprintf(2, "cmp: EOF on %s\n",
					len1 < len2 ? file1 : file2);
			}
			toys.exitval = 1;
			break;
		}
		if (len1 < 1) break;
	}
}

void cmp_main(void)
{
	char *file1 = toys.optargs[0];
	char *file2 = toys.optargs[1];
	int fd1, fd2, size=sizeof(toybuf);
	char *toybuf2 = xmalloc(size);

	fd1 = get_fd(file1);
	fd2 = get_fd(file2);

	do_cmp(fd1, fd2, file1, file2, toybuf, toybuf2, size);

	close(fd1);
	close(fd2);
	free(toybuf2);
}

