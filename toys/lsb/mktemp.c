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
    -t	Prefer $TMPDIR > DIR > /tmp (default DIR > $TMPDIR > /tmp)
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
  int use_dir = (toys.optflags & (FLAG_p|FLAG_t));

  if (!template) {
    template = "tmp.XXXXXXXXXX";
    use_dir = 1;
  }

  // Normally, the precedence is DIR (if set), $TMPDIR (if set), /tmp.
  // With -t it's $TMPDIR, DIR, /tmp.
  if (use_dir) {
    char *tmpdir = getenv("TMPDIR");

    if (toys.optflags & FLAG_t) {
      if (tmpdir && *tmpdir) TT.p = tmpdir;
    } else {
      if (!TT.p || !*TT.p) TT.p = tmpdir;
    }
    if (!TT.p || !*TT.p) TT.p = "/tmp";
  }

  // TODO: coreutils cleans paths, so -p /t/// would result in /t/xxx...
  template = use_dir ? xmprintf("%s/%s", TT.p, template) : xstrdup(template);

  if (toys.optflags & FLAG_u) {
    xputs(mktemp(template));
  } else if (toys.optflags & FLAG_d ? !mkdtemp(template) : mkstemp(template) == -1) {
    if (toys.optflags & FLAG_q) toys.exitval = 1;
    else perror_exit("Failed to create %s %s/%s",
        toys.optflags & FLAG_d ? "directory" : "file", TT.p, template);
  }

  if (CFG_TOYBOX_FREE) free(template);
}
