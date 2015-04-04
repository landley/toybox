/* head.c - copy first lines from input to stdout.
 *
 * Copyright 2006 Timothy Elliott <tle@holymonkey.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/head.html

USE_HEAD(NEWTOY(head, "?n#<0=10", TOYFLAG_USR|TOYFLAG_BIN))

config HEAD
  bool "head"
  default y
  help
    usage: head [-n number] [file...]

    Copy first lines from files to stdout. If no files listed, copy from
    stdin. Filename "-" is a synonym for stdin.

    -n	Number of lines to copy.
*/

#define FOR_head
#include "toys.h"

GLOBALS(
  long lines;
  int file_no;
)

static void do_head(int fd, char *name)
{
  int i, len, lines=TT.lines, size=sizeof(toybuf);

  if (toys.optc > 1) {
    // Print an extra newline for all but the first file
    if (TT.file_no++) xprintf("\n");
    xprintf("==> %s <==\n", name);
    xflush();
  }

  while (lines) {
    len = read(fd, toybuf, size);
    if (len<0) perror_msg("%s",name);
    if (len<1) break;

    for(i=0; i<len;) if (toybuf[i++] == '\n' && !--lines) break;

    xwrite(1, toybuf, i);
  }
}

void head_main(void)
{
  char *arg = *toys.optargs;

  // handle old "-42" style arguments
  if (arg && *arg == '-' && arg[1]) {
    TT.lines = atolx(arg+1);
    toys.optc--;
  } else arg = 0;
  loopfiles(toys.optargs+!!arg, do_head);
}
