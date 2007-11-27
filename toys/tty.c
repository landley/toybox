/* vi: set sw=4 ts=4: */
/* tty.c - print terminal name of stdin
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/tty.html
 */

#include "toys.h"

int tty_main(void)
{
	char *name = ttyname(0);
	if (!toys.optflags) {
		if (name) puts(name);
		else puts("Not a tty");
	}
	return !name;
}
