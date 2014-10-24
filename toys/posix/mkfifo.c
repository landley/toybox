/* mkfifo.c - Create FIFOs (named pipes)
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/mkfifo.html

USE_MKFIFO(NEWTOY(mkfifo, "<1m:"USE_MKFIFO_SMACK("Z:"), TOYFLAG_BIN))

config MKFIFO
  bool "mkfifo"
  default y
  help
    usage: mkfifo [fifo_name...]

    Create FIFOs (named pipes).

config MKFIFO_SMACK
  bool
  default y
  depends on MKFIFO && TOYBOX_SMACK
  help
    usage: mkfifo [-Z context]

    -Z	Set security 'context' to created file

*/

#define FOR_mkfifo
#include "toys.h"

GLOBALS(
  char *arg_context;
  char *m_string;
  mode_t mode;
)

void mkfifo_main(void)
{
  char **s;

  TT.mode = 0666;
  if (toys.optflags & FLAG_m) TT.mode = string_to_mode(TT.m_string, 0);

  for (s = toys.optargs; *s; s++) {
    if (mknod(*s, S_IFIFO | TT.mode, 0) < 0) {
      perror_msg("%s", *s);
    }
    if (CFG_MKFIFO_SMACK) {
      if (toys.optflags & FLAG_Z) {
        if (smack_set_label_for_path(*s, XATTR_NAME_SMACK, 0, TT.arg_context) < 0) {
          unlink(*s);
          error_exit("Unable to create fifo '%s' with '%s' as context.", *s, TT.arg_context);
        }
      }
    }
  }
}
