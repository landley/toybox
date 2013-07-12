/* grep.c - print lines what match given regular expression
 *
 * Copyright 2013 CE Strake <strake888 at gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/cmdbehav.html

USE_GREP(NEWTOY(grep, "EFhinovclqe*f*m#", TOYFLAG_BIN))

config GREP
  bool "grep"
  default n
  help
    usage: grep [-clq] [-EFhinov] (-e RE | -f REfile | RE) [file...]

    modes:
      default: print lines from each file what match regular expression RE.
      -c:   print the number of matching lines in each file.
      -l:   print all matching file names.
      -q:   print nil; quit with code 0 when match found.

    flags:
      -E:   extended RE syntax
      -F:   fixed RE syntax, i.e. all characters literal
      -h:   not print file name
      -i:   case insensitive
      -n:   print line numbers
      -o:   print only matching part
      -v:   invert match
*/

#define FOR_grep
#include "toys.h"
#include <regex.h>
#include <err.h>

/* could be in GLOBALS but so need initialization code */
static int c = 1;

static regex_t re; /* fails in GLOBALS */

GLOBALS(
  long mArgu;
  struct arg_list *fArgu, *eArgu;
  char mode;
)

static void do_grep (int fd, char *name) {
  int n = 0, nMatch = 0;

  for (;;) {
    char *x, *y;
    regmatch_t match;
    int atBOL = 1;

    x = get_rawline (fd, 0, '\n');
    if (!x) break;
    y = x;
    n++; /* start at 1 */

    while (regexec (&re, y, 1, &match, atBOL ? 0 : REG_NOTBOL) == 0) {
      if (atBOL) nMatch++;
      c = 0; atBOL = 0;
      switch (TT.mode) {
      case 'q':
        exit (0);
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
          y += match.rm_so;
          printf ("%.*s\n", match.rm_eo - match.rm_so, y++);
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

void buildRE (void) {
  char *re_xs;

  re_xs = 0;
  for (; TT.eArgu; TT.eArgu = TT.eArgu -> next) {
    if (toys.optflags & FLAG_F) TT.eArgu -> arg = regfix (TT.eArgu -> arg);
    if (re_xs) re_xs = xastrcat (re_xs, "|");
    re_xs = xastrcat (re_xs, TT.eArgu -> arg);
  }
  for (; TT.fArgu; TT.fArgu = TT.fArgu -> next) {
    FILE *f;
    char *x, *y;
    size_t l;

    f = xfopen (TT.fArgu -> arg, "r");
    x = 0;
    for (;;) {
      if (getline (&x, &l, f) < 0) {
        if (feof (f)) break;
        err (2, "failed to read");
      }
      y = x + strlen (x) - 1;
      if (y[0] == '\n') y[0] = 0;

      y = toys.optflags & FLAG_F ? regfix (x) : x;
      if (re_xs) re_xs = xastrcat (re_xs, "|");
      re_xs = xastrcat (re_xs, y);
      free (y);
    }
    free (x);
    fclose (f);
  }

  if (!re_xs) {
    if (toys.optc < 1) errx (2, "no RE");
    re_xs = toys.optflags & FLAG_F ? regfix (toys.optargs[0]) : toys.optargs[0];
    toys.optc--; toys.optargs++;
  }

  if (regcomp (&re, re_xs,
               (toys.optflags & (FLAG_E | FLAG_F) ? REG_EXTENDED : 0) |
               (toys.optflags &  FLAG_i           ? REG_ICASE    : 0)) != 0) {
    errx (2, "bad RE");
  }
}

void grep_main (void) {
  buildRE ();

  if (toys.optflags & FLAG_c) TT.mode = 'c';
  if (toys.optflags & FLAG_l) TT.mode = 'l';
  if (toys.optflags & FLAG_q) TT.mode = 'q';

  if (toys.optc > 0) loopfiles (toys.optargs, do_grep);
  else do_grep (0, "-");

  exit (c);
}
