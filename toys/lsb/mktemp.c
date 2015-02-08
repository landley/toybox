/* mktemp.c - Create a temporary file or directory.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/mktemp.html

USE_MKTEMP(NEWTOY(mktemp, ">1q(directory)d(tmpdir)p:", TOYFLAG_BIN))

config MKTEMP
  bool "mktemp"
  default y
  help
    usage: mktemp [-dq] [-p DIR] [TEMPLATE]

    Safely create a new file and print its name. The default TEMPLATE is
    tmp.XXXXXX. The default DIR is $TMPDIR, or /tmp if $TMPDIR is not set.

    -d, --directory        Create directory instead of file
    -p DIR, --tmpdir=DIR   Put new file in DIR
    -q                     Quiet
*/

#define FOR_mktemp
#include "toys.h"

GLOBALS(
  char * tmpdir;
)

void mktemp_main(void)
{
  int d_flag = toys.optflags & FLAG_d;
  char *template = *toys.optargs;
  int success;

  if (!template) {
    template = "tmp.XXXXXX";
  }

  if (!TT.tmpdir) TT.tmpdir = getenv("TMPDIR");
  if (!TT.tmpdir) TT.tmpdir = "/tmp";

  snprintf(toybuf, sizeof(toybuf), "%s/%s", TT.tmpdir, template);

  if (d_flag ? mkdtemp(toybuf) == NULL : mkstemp(toybuf) == -1) {
    if (toys.optflags & FLAG_q) {
      toys.exitval = 1;
    } else {
      perror_exit("Failed to create temporary %s with template %s/%s",
        d_flag ? "directory" : "file", TT.tmpdir, template);
    }
  }

  xputs(toybuf);
}
