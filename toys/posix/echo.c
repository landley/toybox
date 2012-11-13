/* echo.c - echo supporting -n and -e.
 *
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/echo.html

USE_ECHO(NEWTOY(echo, "^?en", TOYFLAG_BIN))

config ECHO
  bool "echo"
  default y
  help
    usage: echo [-ne] [args...]

    Write each argument to stdout, with one space between each, followed
    by a newline.

    -n	No trailing newline.
    -e	Process the following escape sequences:
    	\\	backslash
    	\0NNN	octal values (1 to 3 digits)
    	\a	alert (beep/flash)
    	\b	backspace
    	\c	stop output here (avoids trailing newline)
    	\f	form feed
    	\n	newline
    	\r	carriage return
    	\t	horizontal tab
    	\v	vertical tab
    	\xHH	hexadecimal values (1 to 2 digits)
*/

#define FOR_echo
#include "toys.h"

void echo_main(void)
{
  int i = 0, out;
  char *arg, *from = "\\abfnrtv", *to = "\\\a\b\f\n\r\t\v", *c;

  for (;;) {
    arg = toys.optargs[i];
    if (!arg) break;
    if (i++) xputc(' ');

    // Should we output arg verbatim?

    if (!(toys.optflags & FLAG_e)) {
      xprintf("%s", arg);
      continue;
    }

    // Handle -e

    for (c=arg;;) {
      if (!(out = *(c++))) break;

      // handle \escapes
      if (out == '\\' && *c) {
        int n = 0, slash = *(c++);
        char *found = strchr(from, slash);
        if (found) out = to[found-from];
        else if (slash == 'c') goto done;
        else if (slash == '0') {
          out = 0;
          while (*c>='0' && *c<='7' && n++<3) out = (out*8)+*(c++)-'0';
        } else if (slash == 'x') {
          out = 0;
          while (n++<2) {
            if (*c>='0' && *c<='9') out = (out*16)+*(c++)-'0';
            else {
              int temp = tolower(*c);
              if (temp>='a' && temp<='f') {
                out = (out*16)+temp-'a'+10;
                c++;
              } else break;
            }
          }
        // Slash in front of unknown character, print literal.
        } else c--;
      }
      xputc(out);
    }
  }

  // Output "\n" if no -n
  if (!(toys.optflags&FLAG_n)) xputc('\n');
done:
  xflush();
}
