/* vi: set sw=4 ts=4: */
/*
 * yes.c - Repeatedly output a string.
 */

#include "toys.h"

int yes_main(void)
{
	for (;;) {
		int i;
		for (i=0; toys.optargs[i]; i++) {
			if (i) xputc(' ');
			xprintf("%s", toys.optargs[i]);
		}
		if (!i) xputc('y');
		xputc('\n');
	}

	return 0;
}
