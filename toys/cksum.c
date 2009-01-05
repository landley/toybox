/* vi: set sw=4 ts=4:
 *
 * cksum.c - produce crc32 checksum value for each input
 *
 * Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/cksum.html

USE_CKSUM(NEWTOY(cksum, "F", TOYFLAG_BIN))

config CKSUM
	bool "cksum"
	default y
	help
	  usage: cksum [-F] [file...]

	  For each file, output crc32 checksum value, length and name of file.
	  If no files listed, copy from stdin.  Filename "-" is a synonym for stdin.

	  -F	Start with 0xffffffff instead of 0.
*/

#include "toys.h"

DEFINE_GLOBALS(
	unsigned crc_table[256];
)

#define TT this.cksum

static unsigned cksum(unsigned crc, unsigned char c)
{
	return (crc<<8)^TT.crc_table[(crc>>24)^c];
}

static void do_cksum(int fd, char *name)
{
	unsigned crc = toys.optflags ? 0xffffffff : 0;
	uint64_t llen = 0, llen2;

	// CRC the data

	for (;;) {
		int len, i;

		len = read(fd, toybuf, sizeof(toybuf));
		if (len<0) {
			perror_msg("%s",name);
			toys.exitval = EXIT_FAILURE;
		}
		if (len<1) break;

		llen += len;
		for (i=0; i<len; i++) crc=cksum(crc, toybuf[i]);
	}

	// CRC the length

	llen2 = llen;
	while (llen) {
		crc = cksum(crc, llen);
		llen >>= 8;
	}

	printf("%u %"PRIu64, ~crc, llen2);
	if (strcmp("-", name)) printf(" %s", name);
	xputc('\n');
}

void cksum_main(void)
{
	crc_init(TT.crc_table);
	loopfiles(toys.optargs, do_cksum);
}
