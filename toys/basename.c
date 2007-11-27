#include "toys.h"

int basename_main(void)
{
	char *name = basename(toys.optargs[0]);
	if (toys.optargs[1]) {
		int slen = strlen(toys.optargs[1]);
		int name_len = strlen(name);
		if (slen < name_len)
			if (!strcmp(name+name_len-slen, toys.optargs[1]))
				*(name+name_len-slen) = '\0';
	}
	puts(name);
	return 0;
}
