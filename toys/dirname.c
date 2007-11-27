/* vi: set sw=4 ts=4: */
/* dirname.c - print directory portion of path, or "." if none.
 *
 * See http://www.opengroup.org/onlinepubs/009695399/utilities/dirname.html
 */

#include "toys.h"
#include <libgen.h>

int dirname_main(void)
{
	puts(dirname(*toys.optargs));
	return 0;
}
