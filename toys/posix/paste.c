/* paste.c - Replace newlines
 *
 * Copyright 2012 Felix Janda <felix.janda@posteo.de>
 *
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/paste.html 
 *
USE_PASTE(NEWTOY(paste, "d:s", TOYFLAG_BIN))

config PASTE
  bool "paste"
  default y
  help
    usage: paste [-s] [-d list] [file...]

    Replace newlines in files.

    -d list    list of delimiters to separate lines
    -s         process files sequentially instead of in parallel

    By default print corresponding lines separated by <tab>.
*/
#define FOR_paste
#include "toys.h"

GLOBALS(
  char *delim;
)

void paste_main(void)
{
  char *p, *buf = toybuf, **args = toys.optargs;
  size_t ndelim = 0;
  int i, j, c;

  // Process delimiter list
  // TODO: Handle multibyte characters
  if (!(toys.optflags & FLAG_d)) TT.delim = "\t";
  for (p = TT.delim; *p; p++, buf++, ndelim++) {
    if (*p == '\\') {
      p++;
      if (-1 == (i = stridx("nt\\0", *p)))
        error_exit("bad delimiter: \\%c", *p);
      *buf = "\n\t\\\0"[i];
    } else *buf = *p;
  }
  *buf = 0;

  if (toys.optflags & FLAG_s) { // Sequential
    FILE *f;

    for (; *args; args++) {
      if ((*args)[0] == '-' && !(*args)[1]) f = stdin;
      else if (!(f = fopen(*args, "r"))) perror_exit("%s", *args);
      for (i = 0, c = 0; c != EOF;) {
        switch(c = getc(f)) {
        case '\n':
          putchar(toybuf[i++ % ndelim]);
        case EOF:
          break;
        default:
          putchar(c);
        }
      }
      if (f != stdin) fclose(f);
      putchar('\n');
    }
  } else { // Parallel
    // Need to be careful not to print an extra line at the end
    FILE **files;
    int anyopen = 1;

    files = (FILE**)(buf + 1);
    for (; *args; args++, files++) {
      if ((*args)[0] == '-' && !(*args)[1]) *files = stdin;
      else if (!(*files = fopen(*args, "r"))) perror_exit("%s", *args);
    }
    while (anyopen) {
      anyopen = 0;
      for (i = 0; i < toys.optc; i++) {
        FILE **f = (FILE**)(buf + 1) + i;

        if (*f) for (;;) {
          c = getc(*f);
          if (c != EOF) {
            if (!anyopen++) for (j = 0; j < i; j++) putchar(toybuf[j % ndelim]);
            if (c != '\n') putchar(c);
            else break;
          }
          else {
            if (*f != stdin) fclose(*f);
            *f = 0;
            break;
          }
        }
        if (anyopen) putchar((i + 1 == toys.optc) ? toybuf[i % ndelim] : '\n');
      }
    }
  }
}
