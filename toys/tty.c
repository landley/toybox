#include "toys.h"

int tty_main(void)
{
	char *name = ttyname(0);
	if (!toys.optflags) {
		if (name)
			puts(name);
		else
			puts("Not a tty");
	}
	return !name;
}
