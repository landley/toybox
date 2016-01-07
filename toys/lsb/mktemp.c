/* mktemp.c - Create a temporary file or directory.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/mktemp.html

USE_MKTEMP(NEWTOY(mktemp, ">1uqd(directory)p(tmpdir):", TOYFLAG_BIN))

config MKTEMP
  bool "mktemp"
  default y
  help
    usage: mktemp [-dqu] [-p DIR] [TEMPLATE]

    Safely create a new file "DIR/TEMPLATE" and print its name.

    -d	Create directory instead of file (--directory)
    -p	Put new file in DIR (--tmpdir)
    -q	Quiet, no error messages
    -u	Don't create anything, just print what would be created

    Each X in TEMPLATE is replaced with a random printable character. The
    default TEMPLATE is tmp.XXXXXX, and the default DIR is $TMPDIR if set,
    else "/tmp".
*/

#define FOR_mktemp
#include "toys.h"

GLOBALS(
  char *tmpdir;
)

void mktemp_main(void)
{
  int d_flag = toys.optflags & FLAG_d;
  char *template = *toys.optargs;

  if (!template) template = "tmp.XXXXXX";

  if (!TT.tmpdir) TT.tmpdir = getenv("TMPDIR");
  if (!TT.tmpdir || !*TT.tmpdir) TT.tmpdir = "/tmp";

  template = strchr(template, '/') ? xstrdup(template)
             : xmprintf("%s/%s", TT.tmpdir, template);

  if (d_flag ? !mkdtemp(template) : mkstemp(template) == -1) {
    if (toys.optflags & FLAG_q) toys.exitval = 1;
    else perror_exit("Failed to create %s %s/%s",
                     d_flag ? "directory" : "file", TT.tmpdir, template);
  } else {
    if (toys.optflags & FLAG_u) unlink(template);
    xputs(template);
  }

  if (CFG_TOYBOX_FREE) free(template);
}
