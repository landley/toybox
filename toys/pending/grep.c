/* grep.c - print lines what match given regular expression
 *
 * Copyright 2013 CE Strake <strake888 at gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/grep.html

USE_GREP(NEWTOY(grep, "EFHabhinosvwclqe*f*m#", TOYFLAG_BIN))
USE_GREP(OLDTOY(egrep, grep, "EFHabhinosvwclqe*f*m#", TOYFLAG_BIN))
USE_GREP(OLDTOY(fgrep, grep, "EFHabhinosvwclqe*f*m#", TOYFLAG_BIN))

config GREP
  bool "grep"
  default n
  help
    usage: grep [-EFivwcloqsHbhn] [-m MAX] [-e REGEX]... [-f REGFILE] [FILE]...

    Show lines matching regular expressions. If no -e, first argument is
    regular expression to match. With no files (or "-" filename) read stdin.
    Returns 0 if matched, 1 if no match found.

    -e  Regex to match. (May be repeated.)
    -f  File containing regular expressions to match.

    match type:
    -E  extended regex syntax    -F  fixed (match literal string)
    -i  case insensitive         -v  invert match
    -w  whole words (implies -E) -m  stop after this many lines matched

    display modes: (default: matched line)
    -c  count of matching lines  -l  show matching filenames
    -o  only matching part       -q  quiet (errors only)
    -s  silent (no error msg)    

    prefix modes (default: filename if checking more than 1 file)
    -H  force filename           -b  byte offset of match
    -h  hide filename            -n  line number of match
*/

#define FOR_grep
#include "toys.h"
#include <regex.h>

static regex_t re; /* fails in GLOBALS */

GLOBALS(
  long m;

  struct arg_list *fArgu, *eArgu;
  char *re_xs;
)

static void do_grep(int fd, char *name)
{
  FILE *file = xfdopen(fd, "r");
  long offset = 0;
  int lcount = 0, mcount = 0, which = toys.optflags & FLAG_w ? 2 : 0;

  for (;;) {
    char *line = 0, *start;
    regmatch_t matches[3];
    size_t len;

    lcount++;
    if (-1 == getline(&line, &len, file)) break;
    len = strlen(line);
    if (len && line[len-1] == '\n') line[len-1] = 0;
    start = line;

    for (;;)
    {
      int rc = regexec(&re, start, 3, matches, start == line ? 0 : REG_NOTBOL);
      int skip = matches[which].rm_eo;

      if (toys.optflags & FLAG_v) {
        if (toys.optflags & FLAG_o) {
          if (rc) skip = matches[which].rm_eo = strlen(start);
          else if (!matches[which].rm_so) {
            start += skip;
            continue;
          } else matches[which].rm_eo = matches[which].rm_so;
        } else {
          if (!rc) break;
          matches[which].rm_eo = strlen(start);
        }
        matches[which].rm_so = 0;
      } else if (rc) break; 

      mcount++;
      if (toys.optflags & FLAG_q) {
        toys.exitval = 0;
        xexit();
      }
      if (toys.optflags & FLAG_l) {
        printf("%s\n", name);
        free(line);
        fclose(file);
        return;
      }
      if (!(toys.optflags & FLAG_c)) {
        if (!(toys.optflags & FLAG_h)) printf("%s:", name);
        if (toys.optflags & FLAG_n) printf("%d:", lcount);
        if (toys.optflags & FLAG_b)
          printf("%ld:", offset + (start-line) +
              ((toys.optflags & FLAG_o) ? matches[which].rm_so : 0));
        if (!(toys.optflags & FLAG_o)) xputs(line);
        else {
          xprintf("%.*s\n", matches[which].rm_eo - matches[which].rm_so,
                  start + matches[which].rm_so);
        }
      }

      start += skip;
      if (!(toys.optflags & FLAG_o) || !*start) break;
    }
    offset += len;

    free(line);

    if ((toys.optflags & FLAG_m) && mcount >= TT.m) break;
  }

  if (toys.optflags & FLAG_c) {
    if (!(toys.optflags & FLAG_h)) printf("%s:", name);
    xprintf("%d\n", mcount);
  }

  // loopfiles will also close the fd, but this frees an (opaque) struct.
  fclose(file);
}

char *regfix(char *re_xs)
{
  char *re_ys;
  int ii, jj = 0;

  re_ys = xmalloc(2*strlen (re_xs) + 1);
  for (ii = 0; re_xs[ii]; ii++) {
    if (strchr("^.[]$()|*+?{}\\", re_xs[ii])) re_ys[jj++] = '\\';
    re_ys[jj++] = re_xs[ii];
  }
  re_ys[jj] = 0;

  return re_ys;
}

void addRE(char *x)
{
  if (toys.optflags & FLAG_F) x = regfix(x);
  if (TT.re_xs) TT.re_xs = xastrcat(TT.re_xs, "|");
  TT.re_xs = xastrcat(TT.re_xs, x);
  if (toys.optflags & FLAG_F) free(x);
}

void buildRE(void)
{
  for (; TT.eArgu; TT.eArgu = TT.eArgu -> next) addRE(TT.eArgu -> arg);
  for (; TT.fArgu; TT.fArgu = TT.fArgu -> next) {
    FILE *f;
    char *x, *y;
    size_t l;

    f = xfopen(TT.fArgu -> arg, "r");
    x = 0;
    for (;;) {
      if (getline (&x, &l, f) < 0) {
        if (feof(f)) break;
        toys.exitval = 2;
        perror_exit("failed to read");
      }
      y = x + strlen(x) - 1;
      if (y[0] == '\n') y[0] = 0;

      addRE(x);
    }
    free(x);
    fclose(f);
  }

  if (!TT.re_xs) {
    if (toys.optc < 1) {
      toys.exitval = 2;
      error_exit("no RE");
    }
    TT.re_xs = (toys.optflags & FLAG_F) ? regfix(toys.optargs[0])
        : toys.optargs[0];
    toys.optc--; toys.optargs++;
  }

  TT.re_xs = xmsprintf((toys.optflags & FLAG_w)
      ? "(^|[^_[:alnum:]])(%s)($|[^_[:alnum:]])" : "%s", TT.re_xs);

  if (regcomp(&re, TT.re_xs,
               ((toys.optflags & (FLAG_E | FLAG_F)) ? REG_EXTENDED : 0) |
               ((toys.optflags &  FLAG_i)           ? REG_ICASE    : 0)) != 0) {
    toys.exitval = 2;
    error_exit("bad RE");
  }
}

void grep_main(void)
{
  // Handle egrep and fgrep
  if (*toys.which->name == 'e' || (toys.optflags & FLAG_w))
    toys.optflags |= FLAG_E;
  if (*toys.which->name == 'f') toys.optflags |= FLAG_F;

  buildRE();

  if (!(toys.optflags & FLAG_H) && (toys.optc < 2)) toys.optflags |= FLAG_h;

  toys.exitval = 1;
  if (toys.optflags & FLAG_s) {
    close(2);
    xopen("/dev/null", O_RDWR);
  }
  loopfiles_rw(toys.optargs, O_RDONLY, 0, 1, do_grep);
  xexit();
}
