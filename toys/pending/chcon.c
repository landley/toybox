/* chcon.c - Change file security context
 *
 * Copyright 2014 The Android Open Source Project

USE_CHCON(NEWTOY(chcon, "<1hRv", TOYFLAG_USR|TOYFLAG_BIN))

config CHCON
  bool "chcon"
  depends on TOYBOX_SELINUX
  default y
  help
    usage: chcon [-hRv] CONTEXT FILE...

    Change the SELinux security context of listed file[s] (recursively with -R).

    -h change symlinks instead of what they point to.
    -R recurse into subdirectories.
    -v verbose output.
*/

#define FOR_chcon
#include "toys.h"

GLOBALS(
  char *context;
)

int do_chcon(struct dirtree *try)
{
  int ret;

  if (!dirtree_notdotdot(try)) return 0;

  char *path = dirtree_path(try, 0);
  if (toys.optflags & FLAG_v)
    printf("chcon '%s' to %s\n", path, TT.context);
  ret = ((toys.optflags&FLAG_h) ? lsetfilecon : setfilecon)(path, TT.context);
  if (ret == -1)
    perror_msg("'%s' to %s", path, TT.context);
  free(path);

  return (toys.optflags & FLAG_R) ? DIRTREE_RECURSE : 0;
}

void chcon_main(void)
{
  TT.context = *toys.optargs;
  char **file;

  for (file = toys.optargs+1; *file; file++) dirtree_read(*file, do_chcon);
}
