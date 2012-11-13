/* unlink.c - delete one file
 *
 * Copyright 2011 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/unlink.html

USE_UNLINK(NEWTOY(unlink, "<1>1", TOYFLAG_USR|TOYFLAG_BIN))

config UNLINK
  bool "unlink"
  default y
  help
    usage: unlink FILE

    Deletes one file.
*/

#include "toys.h"

void unlink_main(void)
{
  if (unlink(*toys.optargs))
    perror_exit("Couldn't unlink `%s'", *toys.optargs);
}
