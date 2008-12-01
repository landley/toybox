/* vi: set sw=4 ts=4:
 *
 * uname.c - return system name
 *
 * Copyright 2008 Rob Landley <rob@landley.net>
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/uname.html

USE_UNAME(NEWTOY(uname, "amvrns", TOYFLAG_BIN))

config UNAME
	bool "uname"
	default y
	help
	  usage: uname [-asnrvmpio]

	  Print system information.

	  -s	System name
	  -n	Network (domain) name
	  -r	Release number
	  -v	Version (build date)
	  -m	Machine (hardware) name
	  -a	All of the above
*/

#include "toys.h"
#include <sys/utsname.h>

// If a 32 bit x86 build environment working in a chroot under an x86-64
// kernel returns x86_64 for -m it confuses ./configure.  Special case it.

#if defined(__i686__)
#define GROSS "i686"
#elif defined(__i586__)
#define GROSS "i586"
#elif defined(__i486__)
#define GROSS "i486"
#elif defined(__i386__)
#define GROSS "i386"
#endif

#define FLAG_a (1<<5)

void uname_main(void)
{
	int i, flags = toys.optflags, needspace=0;

	uname((void *)toybuf);

	if (!flags) flags=1;
	for (i=0; i<5; i++) {
		char *c = toybuf+(65*i);

		if (flags & ((1<<i)|FLAG_a)) {
			int len = strlen(c);

			// This problem originates in autoconf, so of course the solution
			// is horribly ugly.
#ifdef GROSS
			if (i==4 && !strcmp(c,"x86_64")) printf(GROSS);
	        else
#endif

			if (needspace++) {
				// We can't decrement on the first entry, because
				// needspace would be 0
				*(--c)=' ';
				len++;
			}
			xwrite(1, c, len);
		}
	}
	putchar('\n');
}
