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
  struct pollfd pfd = {0, POLLIN, 0};
  unsigned long long size = 0, last = 0, then = 0, now;
  char *buf = xmalloc(65536);
  int len;

  // poll, print if data not ready, update 4x/second otherwise
  for (;;) {
    if (!(len = poll(&pfd, 1, (last != size) ? 250 : 0))) continue;
    if (len<0 && errno != EINTR && errno != ENOMEM) perror_exit(0);
    if ((len = xread(0, buf, 65536))) {
      xwrite(1, buf, len);
      size += len;
      if ((now = millitime())-then<250) continue;
    }
    dprintf(2, "%llu bytes\r", size);
    if (!len) break;
  }
  dprintf(2, "\n");
}
