/* vi: set ts=4:
 *  Call regcomp() and handle errors.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * This is a separate file so environments that haven't got regular expression
 * support can configure this out and avoid a build break.
 */

#include "toys.h"
#include "xregcomp.h"

void xregcomp(regex_t *preg, char *regex, int cflags)
{
	int rc = regcomp(preg, regex, cflags);

	if (rc) {
		char msg[256];
		regerror(rc, preg, msg, 255);
		msg[255]=0;
		error_exit("xregcomp: %s", msg);
	}
}
