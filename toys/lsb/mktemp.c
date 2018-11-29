/* mktemp.c - Create a temporary file or directory.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/mktemp.html

USE_MKTEMP(NEWTOY(mktemp, ">1uqd(directory)p(tmpdir):t", TOYFLAG_BIN))

config MKTEMP
  bool "mktemp"
  default y
  help
    usage: mktemp [-dqu] [-p DIR] [TEMPLATE]

    Safely create a new file "DIR/TEMPLATE" and print its name.

    -d	Create directory instead of file (--directory)
    -p	Put new file in DIR (--tmpdir)
    -q	Quiet, no error messages
    -t	Prepend $TMPDIR or /tmp if unset
    -u	Don't create anything, just print what would be created

    Each X in TEMPLATE is replaced with a random printable character. The
    default TEMPLATE is tmp.XXXXXXXXXX.
*/

#define FOR_mktemp
#include "toys.h"

GLOBALS(
  char *p;
)

void mktemp_main(void)
{
  char *template = *toys.optargs;

  if (!template) {
    toys.optflags |= FLAG_t;
    template = "tmp.XXXXXXXXXX";
  }

  if (!TT.p || (toys.optflags & FLAG_t)) TT.p = getenv("TMPDIR");
  if (!TT.p || !*TT.p) TT.p = "/tmp";

  // TODO: coreutils cleans paths, so -p /t/// would result in /t/xxx...
  template = (strchr(template, '/') || !(toys.optflags & (FLAG_p|FLAG_t)))
      ? xstrdup(template) : xmprintf("%s/%s", TT.p, template);

  if (toys.optflags & FLAG_u) {
    mktemp(template);
    xputs(template);
  } else if (toys.optflags & FLAG_d ? !mkdtemp(template) : mkstemp(template) == -1) {
    if (toys.optflags & FLAG_q) toys.exitval = 1;
    else perror_exit("Failed to create %s %s/%s",
        toys.optflags & FLAG_d ? "directory" : "file", TT.p, template);
  }

  if (CFG_TOYBOX_FREE) free(template);
}
