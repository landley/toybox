/* echo.c - echo supporting -n and -e.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/echo.html
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
  int i = 0, out;
  char *arg, *c;

  for (;;) {
    arg = toys.optargs[i];
    if (!arg) break;
    if (i++) putchar(' ');

    // Should we output arg verbatim?

    if (!FLAG(e)) {
      xprintf("%s", arg);
      continue;
    }

    // Handle -e

    for (c = arg;;) {
      if (!(out = *(c++))) break;

      // handle \escapes
      if (out == '\\' && *c) {
        int slash = *(c++), n = unescape(slash);

        if (n) out = n;
        else if (slash=='c') return;
        else if (slash=='0') {
          out = 0;
          while (*c>='0' && *c<='7' && n++<3) out = (out*8)+*(c++)-'0';
        } else if (slash=='x') {
          out = 0;
          while (n++<2) {
            if (*c>='0' && *c<='9') out = (out*16)+*(c++)-'0';
            else {
              int temp = tolower(*c);
              if (temp>='a' && temp<='f') {
                out = (out*16)+temp-'a'+10;
                c++;
              } else {
                if (n==1) {
                  --c;
                  out = '\\';
                }
                break;
              }
            }
          }
        // Slash in front of unknown character, print literal.
        } else c--;
      }
      putchar(out);
    }
  }

  // Output "\n" if no -n
  if (!FLAG(n)) putchar('\n');
}
