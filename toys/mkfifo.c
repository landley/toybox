/* vi: set sw=4 ts=4: */
/*
 * mkfifo.c: Create a named pipe.
 */

#include "toys.h"

int mkfifo_main(void)
{
	char *arg;
	int i;
	mode_t mode;

	if (toys.optflags) {
		long temp;
		char *end;
		int len = strlen(toy.mkfifo.mode);
		temp = strtol(toy.mkfifo.mode, &end, 8);
		switch (temp) {
			case LONG_MAX:
			case LONG_MIN:
			case 0:
				if (!errno)
					break;
				error_exit("Invalid mode");
		}
		if (temp > 0777 || *end || len < 3 || len > 4)
			error_exit("Invalid mode");
		mode = (mode_t)temp;
	} else {
		mode = 0644;
	}

	for (i = 0; (arg = toys.optargs[i]); i++)
		if (mkfifo(arg, mode))
			perror_exit(arg);

	return 0;
}
