/* echo.c - echo supporting -n and -e.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/echo.html
 *
 * Deviations from posix: we parse command line options, as Linux has
 * consistently done since 1992. Posix defaults -e to on, we require -e.
 * We also honor -- to _stop_ option parsing (bash doesn't, we go with
 * consistency over compatibility here).

USE_ECHO(NEWTOY(echo, "^?Een[-eE]", TOYFLAG_BIN|TOYFLAG_MAYFORK))

config ECHO
  bool "echo"
  default y
  help
    usage: echo [-neE] [ARG...]

    Write each argument to stdout, with one space between each, followed
    by a newline.

    -n	No trailing newline
    -E	Print escape sequences literally (default)
    -e	Process the following escape sequences:
    	\\	Backslash
    	\0NNN	Octal values (1 to 3 digits)
    	\a	Alert (beep/flash)
    	\b	Backspace
    	\c	Stop output here (avoids trailing newline)
    	\f	Form feed
    	\n	Newline
    	\r	Carriage return
    	\t	Horizontal tab
    	\v	Vertical tab
    	\xHH	Hexadecimal values (1 to 2 digits)
*/

#define FOR_echo
#include "toys.h"

void echo_main(void)
{
  int i = 0;
  char *arg, *c, out[8];

  while ((arg = toys.optargs[i])) {
    if (i++) putchar(' ');

    // Should we output arg verbatim?

    if (!FLAG(e)) {
      xprintf("%s", arg);
      continue;
    }

    // Handle -e

    for (c = arg; *c; ) {
      unsigned u;

      if (*c == '\\' && c[1] == 'c') return;
      if ((u = unescape2(&c, 1))<128) putchar(u);
      else printf("%.*s", (int)wcrtomb(out, u, 0), out);
    }
  }

  // Output "\n" if no -n
  if (!FLAG(n)) putchar('\n');
}
