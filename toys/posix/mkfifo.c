/* mkfifo.c - Create FIFOs (named pipes)
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/mkfifo.html

USE_MKFIFO(NEWTOY(mkfifo, "<1m:", TOYFLAG_USR|TOYFLAG_BIN))

config MKFIFO
  bool "mkfifo"
  default y
  help
    usage: mkfifo [fifo_name...]

    Create FIFOs (named pipes).
*/

#define FOR_mkfifo
#include "toys.h"

GLOBALS(
  char *m_string;
  mode_t mode;
)

void mkfifo_main(void)
{
  char **s;

  TT.mode = 0666;
  if (toys.optflags & FLAG_m) TT.mode = string_to_mode(TT.m_string, 0);

  for (s = toys.optargs; *s; s++)
    if (mknod(*s, S_IFIFO | TT.mode, 0) < 0) perror_msg("%s", *s);
}
