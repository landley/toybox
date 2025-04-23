/* mkfifo.c - Create FIFOs (named pipes)
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/mkfifo.html

USE_MKFIFO(NEWTOY(mkfifo, "<1"SKIP_TOYBOX_LSM_NONE("Z:")"m:", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_MOREHELP(!CFG_TOYBOX_LSM_NONE)))

config MKFIFO
  bool "mkfifo"
  default y
  help
    usage: mkfifo ![!-!Z! !C!O!N!T!E!X!T!]! [NAME...]

    Create FIFOs (named pipes).
    !
    !-Z	Security context
*/

#define FOR_mkfifo
#include "toys.h"

GLOBALS(
  char *m, *Z;

  mode_t mode;
)

void mkfifo_main(void)
{
  char **s;

  TT.mode = 0666;
  if (FLAG(m)) TT.mode = string_to_mode(TT.m, 0);

  if (FLAG(Z)) if (0>lsm_set_create(TT.Z)) perror_exit("-Z '%s' failed", TT.Z);

  for (s = toys.optargs; *s; s++)
    if (mknod(*s, S_IFIFO | TT.mode, 0) < 0) perror_msg_raw(*s);
}
