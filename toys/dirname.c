#include "toys.h"
#include <libgen.h>

int dirname_main(void)
{
	puts(dirname(toys.optargs[0]));
	return 0;
}
