/* vi: set sw=4 ts=4: */
/* basename.c - print non-directory portion of path
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/basename.html
 */

#include "toys.h"

int basename_main(void)
{
	char *name = basename(*toys.optargs);
	char *suffix = toys.optargs[1];
	if (suffix) {
		char *end = name+strlen(name)-strlen(suffix);
		if (end>name && !strcmp(end,suffix)) *end=0;
	}
	puts(name);
	return 0;
}
