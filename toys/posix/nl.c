/* nl.c - print line numbers
 *
 * Copyright 2013 CE Strake <strake888@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/nl.html
 *
 * This implements a subset: only one logical page (-ip), no sections (-dfh).
 * todo: -lv

USE_NL(NEWTOY(nl, "v#<1=1l#b:n:s:w#<0=6E", TOYFLAG_BIN))

config NL
  bool "nl"
  default y
  help
    usage: nl [-E] [-l #] [-b MODE] [-n STYLE] [-s SEPARATOR] [-w WIDTH] [FILE...]

    Number lines of input.

    -E	Use extended regex syntax (when doing -b pREGEX)
    -b	which lines to number: a (all) t (non-empty, default) pREGEX (pattern)
    -l	Only count last of this many consecutive blank lines
    -n	number STYLE: ln (left justified) rn (right justified) rz (zero pad)
    -s	Separator to use between number and line (instead of TAB)
    -w	Width of line numbers (default 6)
*/

#define FOR_nl
#include "toys.h"

GLOBALS(
  long w;
  char *s;
  char *n;
  char *b;
  long l;
  long v;

  // Count of consecutive blank lines for -l has to persist between files
  long lcount;
)

static void do_nl(int fd, char *name)
{
  FILE *f = xfdopen(fd, "r");
  int w = TT.w, slen = strlen(TT.s);

  for (;;) {
    char *line = 0;
    size_t temp;
    int match = *TT.b != 'n';

    if (getline(&line, &temp, f) < 1) {
      if (ferror(f)) perror_msg_raw(name);
      break;
    }

    if (*TT.b == 'p') match = !regexec((void *)(toybuf+16), line, 0, 0, 0);
    if (TT.l || *TT.b == 't')
      if (*line == '\n') match = TT.l && ++TT.lcount >= TT.l;
    if (match) {
      TT.lcount = 0;
      printf(toybuf, w, TT.v++, TT.s);
    } else printf("%*c", (int)w+slen, ' ');
    xprintf("%s", line);

    free(line);
  }

  fclose(f);
}

void nl_main(void)
{
  char *clip = "";

  if (!TT.s) TT.s = "\t";

  if (!TT.n || !strcmp(TT.n, "rn")); // default
  else if (!strcmp(TT.n, "ln")) clip = "-";
  else if (!strcmp(TT.n, "rz")) clip = "0";
  else error_exit("bad -n '%s'", TT.n);

  sprintf(toybuf, "%%%s%s", clip, "*ld%s");

  if (!TT.b) TT.b = "t";
  if (*TT.b == 'p' && TT.b[1])
    xregcomp((void *)(toybuf+16), TT.b+1,
      REG_NOSUB | (toys.optflags&FLAG_E)*REG_EXTENDED);
  else if (!strchr("atn", *TT.b)) error_exit("bad -b '%s'", TT.b);

  loopfiles (toys.optargs, do_nl);
}
