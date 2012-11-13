/* count.c - Progress indicator from stdin to stdout
 *
 * Copyright 2002 Rob Landley <rob@landley.net>

USE_COUNT(NEWTOY(count, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config COUNT
  bool "count"
  default y
  help
    usage: count

    Copy stdin to stdout, displaying simple progress indicator to stderr.
*/

#include "toys.h"

void count_main(void)
{
  uint64_t size = 0;
  int len;
  char buf[32];

  for (;;) {
    len = xread(0, toybuf, sizeof(toybuf));
    if (!len) break;
    size += len;
    xwrite(1, toybuf, len);
    xwrite(2, buf, sprintf(buf, "%"PRIu64" bytes\r", size));
  }
  xwrite(2, "\n", 1);
}
