/*strings.c - print the strings of printable characters in files.
 *
 * Copyright 2014 Kyung-su Kim <kaspyx@gmail.com>
 * Copyright 2014 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard
 * TODO: utf8 strings
 * TODO: posix -t

USE_STRINGS(NEWTOY(strings, "an#=4<1fo", TOYFLAG_USR|TOYFLAG_BIN))

config STRINGS
  bool "strings"
  default y
  help
    usage: strings [-fo] [-n LEN] [FILE...]

    Display printable strings in a binary file

    -f	Precede strings with filenames
    -n	At least LEN characters form a string (default 4)
    -o	Precede strings with decimal offsets
*/

#define FOR_strings
#include "toys.h"

GLOBALS(
  long num;
)

void do_strings(int fd, char *filename)
{
  int nread, i, wlen = TT.num, count = 0;
  off_t offset = 0;
  char *string = xzalloc(wlen + 1);

  for (;;) {
    nread = read(fd, toybuf, sizeof(toybuf));
    if (nread < 0) perror_msg("%s", filename);
    if (nread < 1) break;
    for (i = 0; i < nread; i++, offset++) {
      if (((toybuf[i] >= 32) && (toybuf[i] <= 126)) || (toybuf[i] == '\t')) {
        if (count == wlen) fputc(toybuf[i], stdout);
        else {
          string[count++] = toybuf[i];
          if (count == wlen) {
            if (toys.optflags & FLAG_f) printf("%s: ", filename);
            if (toys.optflags & FLAG_o)
              printf("%7lld ",(long long)(offset - wlen));
            printf("%s", string);
          }
        }
      } else {
        if (count == wlen) xputc('\n');
        count = 0;
      }
    }
  }
  xclose(fd);
  free(string);
}

void strings_main(void)
{
  loopfiles(toys.optargs, do_strings);
}
