/* grep.c - print lines what match given regular expression
 *
 * Copyright 2013 CE Strake <strake888 at gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cmdbehav.html

USE_GREP(NEWTOY(grep, "EFHahinosvwclqe*f*m#", TOYFLAG_BIN))

config GREP
  bool "grep"
  default n
  help
    usage: grep [-clq] [-EFHhinosvw] (-e RE | -f REfile | RE) [file...]

    modes:
      default: print lines from each file what match regular expression RE.
      -c:   print the number of matching lines in each file.
      -l:   print all matching file names.
      -q:   print nil; quit with code 0 when match found.

    flags:
      -E:   extended RE syntax
      -F:   fixed RE syntax, i.e. all characters literal
      -H:   print file name
      -h:   not print file name
      -i:   case insensitive
      -n:   print line numbers
      -o:   print only matching part
      -s:   keep silent on error
      -v:   invert match
      -w:   match full word only
*/

#define FOR_grep
#include "toys.h"
#include <regex.h>

static regex_t re; /* fails in GLOBALS */

GLOBALS(
  long mArgu;
  struct arg_list *fArgu, *eArgu;
  char mode, *re_xs;
)

static void do_grep (int fd, char *name) {
  int n = 0, nMatch = 0;

  for (;;) {
    char *x, *y;
    regmatch_t matches[3];
    int atBOL = 1;

    x = get_rawline (fd, 0, '\n');
    if (!x) break;
    y = x;
    n++; /* start at 1 */

    while (regexec (&re, y, 3, matches, atBOL ? 0 : REG_NOTBOL) == 0) {
      if (atBOL) nMatch++;
      toys.exitval = 0;
      atBOL = 0;
      switch (TT.mode) {
      case 'q':
        xexit ();
      case 'l':
        if (!(toys.optflags & FLAG_h)) printf ("%s\n", name);
        free (x);
        return;
      case 'c':
        break;
      default:
        if (!(toys.optflags & FLAG_h)) printf ("%s:", name);
        if ( (toys.optflags & FLAG_n)) printf ("%d:", n);
        if (!(toys.optflags & FLAG_o)) fputs (x, stdout);
        else {
          y += matches[2].rm_so;
          printf ("%.*s\n", matches[2].rm_eo - matches[2].rm_so, y++);
        }
      }
      if (!(toys.optflags & FLAG_o)) break;
    }

    free (x);

    if ((toys.optflags & FLAG_m) && nMatch >= TT.mArgu) break;
  }

  if (TT.mode == 'c') printf ("%s:%d\n", name, nMatch);
}

char *regfix (char *re_xs) {
  char *re_ys;
  int ii, jj = 0;
  re_ys = xmalloc (2*strlen (re_xs) + 1);
  for (ii = 0; re_xs[ii]; ii++) {
    if (strchr ("^.[]$()|*+?{}\\", re_xs[ii])) re_ys[jj++] = '\\';
    re_ys[jj++] = re_xs[ii];
  }
  re_ys[jj] = 0;
  return re_ys;
}

void addRE (char *x) {
  if (toys.optflags & FLAG_F) x = regfix (x);
  if (TT.re_xs) TT.re_xs = astrcat (TT.re_xs, "|");
  TT.re_xs = astrcat (TT.re_xs, x);
  if (toys.optflags & FLAG_F) free (x);
}

void buildRE (void) {
  for (; TT.eArgu; TT.eArgu = TT.eArgu -> next) addRE (TT.eArgu -> arg);
  for (; TT.fArgu; TT.fArgu = TT.fArgu -> next) {
    FILE *f;
    char *x, *y;
    size_t l;

    f = xfopen (TT.fArgu -> arg, "r");
    x = 0;
    for (;;) {
      if (getline (&x, &l, f) < 0) {
        if (feof (f)) break;
        toys.exitval = 2;
        perror_exit ("failed to read");
      }
      y = x + strlen (x) - 1;
      if (y[0] == '\n') y[0] = 0;

      addRE (x);
    }
    free (x);
    fclose (f);
  }

  if (!TT.re_xs) {
    if (toys.optc < 1) {
      toys.exitval = 2;
      error_exit ("no RE");
    }
    TT.re_xs = toys.optflags & FLAG_F ? regfix (toys.optargs[0]) : toys.optargs[0];
    toys.optc--; toys.optargs++;
  }

  TT.re_xs = xmsprintf (toys.optflags & FLAG_w ? "(^|[^_[:alnum:]])(%s)($|[^_[:alnum:]])" : "()(%s)()", TT.re_xs);

  if (regcomp (&re, TT.re_xs,
               (toys.optflags & (FLAG_E | FLAG_F) ? REG_EXTENDED : 0) |
               (toys.optflags &  FLAG_i           ? REG_ICASE    : 0)) != 0) {
    toys.exitval = 2;
    error_exit ("bad RE");
  }
}

void grep_main (void) {
  buildRE ();

  if (toys.optflags & FLAG_c) TT.mode = 'c';
  if (toys.optflags & FLAG_l) TT.mode = 'l';
  if (toys.optflags & FLAG_q) TT.mode = 'q';

  if (!(toys.optflags & FLAG_H) && (toys.optc < 2)) toys.optflags |= FLAG_h;

  toys.exitval = 1;
  loopfiles_rw (toys.optargs, O_RDONLY, 0, toys.optflags & FLAG_s, do_grep);
  xexit ();
}
