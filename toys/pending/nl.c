/* nl.c - print line numbers
 *
 * Copyright 2013 CE Strake <strake888@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cmdbehav.html

USE_NL(NEWTOY(nl, "Eb:n:s:w#", TOYFLAG_BIN))

config NL
  bool "nl"
  default n
  help
    usage: nl [-E] [-b mode] [-n (r|l)(n|z)] [-s separator] [-w width]

    modes: give numbers to
      a:   all lines
      t:   non-empty lines
      n:   no lines
      pRE: lines what match regex RE

    flags:
      E:   extended RE syntax
*/

#define FOR_nl
#include "toys.h"
#include <regex.h>

GLOBALS(
  long wArgu;
  char *sArgu, *nArgu, *bArgu, *re_xs;
  char fmt[5];
)

char s = '\t';
long width = 6;
regex_t re; /* fails in GLOBALS */

void do_nl (int fd, char *name) {
  char *x;
  FILE *f;
  long n = 0;

  f = fdopen (fd, "r");
  if (!f) perror_exit ("failed to open %s", name);

  x = 0;
  for (;;) {
    size_t l;
    if (getline (&x, &l, f) < 0) {
      if (feof (f)) break;
      perror_exit ("failed to read");
    }
    if (TT.re_xs && regexec (&re, x, 0, 0, 0) == 0) printf (TT.fmt, width, ++n);
    printf ("%c%s", s, x);
  }

  free (x);
  fclose (f);
}

void nl_main (void) {
  if (toys.optflags & FLAG_w) width = TT.wArgu;

  if (TT.sArgu) s = TT.sArgu[0];

  if (!TT.nArgu) TT.nArgu = "rn";

  if (TT.bArgu) switch (TT.bArgu[0]) {
  case 'a':
    TT.re_xs = "";
    break;
  case 't':
    TT.re_xs = ".\n";
    break;
  case 'n':
    TT.re_xs = 0;
    break;
  case 'p':
    TT.re_xs = TT.bArgu + 1;
    break;
  default:
    error_exit ("bad mode: %c", TT.bArgu[0]);
  }
  else TT.re_xs = ".\n";

  if (TT.re_xs &&
      regcomp (&re, TT.re_xs,
               REG_NOSUB |
               (toys.optflags & FLAG_E ? REG_EXTENDED : 0)) != 0) {
    error_exit ("bad RE");
  }

  strcpy (TT.fmt, "%");
  if (TT.nArgu[0] == 'l') strcat (TT.fmt, "-");
  if (TT.nArgu[1] == 'z') strcat (TT.fmt, "0");
  strcat (TT.fmt, "*d");

  loopfiles (toys.optargs, do_nl);
}
