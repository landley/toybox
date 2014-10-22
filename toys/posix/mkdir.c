/* mkdir.c - Make directories
 *
 * Copyright 2012 Georgi Chorbadzhiyski <georgi@unixsol.org>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/mkdir.html

USE_MKDIR(NEWTOY(mkdir, "<1vpm:"USE_MKDIR_SMACK("Z:"), TOYFLAG_BIN|TOYFLAG_UMASK))

config MKDIR
  bool "mkdir"
  default y
  help
    usage: mkdir [-vp] [-m mode] [dirname...]

    Create one or more directories.

    -m	set permissions of directory to mode.
    -p	make parent directories as needed.
    -v	verbose

config MKDIR_SMACK
  bool
  default y
  depends on PS && TOYBOX_SMACK
  help
    usage: mkdir [-Z context] 

    -Z	set security context.
*/

#define FOR_mkdir
#include "toys.h"

GLOBALS(
  char *arg_context;
  char *arg_mode;
)

void mkdir_main(void)
{
  char **s;
  mode_t mode = (0777&~toys.old_umask);
  int mkflag;

  if (TT.arg_mode) mode = string_to_mode(TT.arg_mode, 0777);

  if (CFG_MKDIR_SMACK) {
    if ((toys.optflags & FLAG_Z) && (toys.optflags & FLAG_p)) {
     /* This changes current process smack label.
      * All directories created later by this process will get access label
      * equal to process label that they were created by.
      */
      if (smack_set_label_for_self(TT.arg_context) < 0)
        error_exit("Unable to set the context to '%s'.", TT.arg_context);
    }
  }

  // Note, -p and -v flags AREN't line up with mkpathat() flags

  mkflag = 1;
  if (toys.optflags & FLAG_p) mkflag |= 2;
  if (toys.optflags & FLAG_v) mkflag |= 4;
  for (s=toys.optargs; *s; s++) {
    if (mkpathat(AT_FDCWD, *s, mode, mkflag)) {
      perror_msg("'%s'", *s);
    }
    if (CFG_MKDIR_SMACK) {
      if (toys.optflags & FLAG_Z) {
        if (smack_set_label_for_path(*s, XATTR_NAME_SMACK, 0, TT.arg_context) < 0) {
          rmdir(*s);
          error_exit("Unable to create directory '%s' with '%s' as context.", *s, TT.arg_context);
        }
      }
    }
  }
}
