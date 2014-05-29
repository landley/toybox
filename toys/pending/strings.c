/*strings.c - print the strings of printable characters in files.
 *
 * Copyright 2014 Kyung-su Kim <kaspyx@gmail.com>
 * Copyright 2014 Kyungwan Han <asura321@gmail.com>
 *
 * No Standard

USE_STRINGS(NEWTOY(strings, "n#=4<1fo", TOYFLAG_USR|TOYFLAG_BIN))

config STRINGS
  bool "strings"
  default n
  help
    usage: strings [-fo] [-n count] [FILE] ...

    Display printable strings in a binary file

    -f Precede strings with filenames
    -n [LEN] At least LEN characters form a string (default 4)
    -o Precede strings with decimal offsets
*/
#define FOR_strings
#include "toys.h"

GLOBALS(
  long num;
)

void do_strings(int fd, char *filename)
{
  int nread, i, wlen, count = 0;
  off_t offset = 0;
  char *string;

  string = xzalloc(TT.num + 1);
  wlen = TT.num - 1;

  while ((nread = read(fd,toybuf,sizeof(toybuf)))>0 ) {
    for (i = 0; i < nread; i++, offset++) {
      if (((toybuf[i] >= 32) && (toybuf[i] <= 126)) || (toybuf[i] == '\t')) {
        if (count > wlen) xputc(toybuf[i]);
        else {
          string[count] = toybuf[i];
          if (count == wlen) {
            if (toys.optflags & FLAG_f) xprintf("%s: ", filename);
            if (toys.optflags & FLAG_o)
              xprintf("%7lld ",(long long)(offset - wlen));
            xprintf("%s",string);
          }
          count++;
        }
      } else {
        if (count > wlen) xputc('\n');
        count = 0;
      }
    }
  }
  xclose(fd);
  free(string);
}

void strings_main(void)
{
  loopfiles(toys.optargs,  do_strings);
}

