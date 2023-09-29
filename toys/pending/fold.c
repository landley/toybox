/* fold.c - filter for folding lines
 *
 * Copyright 2023 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/fold.html

USE_FOLD(NEWTOY(fold, "bsw#<1=80", TOYFLAG_USR|TOYFLAG_BIN))

config FOLD
  bool "fold"
  default n
  help
    usage: fold [-bsSu] [-w WIDTH] [FILE...]

    Break long lines by inserting newlines.

    -b	Count bytes instead of utf-8 unicode columns
    -s	Wrap at whitespace when possible
    -w	Break at WIDTH columns (default 80)
*/

#define FOR_fold
#include "toys.h"

GLOBALS(
  long w;

  // Bitfield of variable width character positions, for backspace.
  char *bs;
)

// wcwidth utf8towc
void do_fold(int fd, char *name)
{
  FILE *fp = fd ? fdopen(fd, "r") : stdin;
  char *rr, *ss;
  long ii, bb, ww, width, space;
  unsigned cc;

  // Note: not bothering to handle embedded NUL bytes, they truncate the line.

  // Loop reading/printing lines
  while ((ss = rr = xgetdelim(fp, '\n'))) for (ii = width = space = 0;;) {
    if (!ii) memset(TT.bs, 0, (TT.w+7)/8);

    // Parse next character's byte length and column width
    bb = ww = 1;
    if (ss[ii]<32) ww = 0;
    if (FLAG(b)) cc = ss[ii];
    else if ((bb = utf8towc(&cc, ss+ii, 4))>0 && (ww = wcwidth(cc))<0) ww = 0;
    if (cc=='\t') ww = 8-(width&7);

    // Did line end?
    if (!cc || cc=='\r' || cc=='\n') {
      if (cc) ii++;
      if (ii) {
        xwrite(1, ss, ii);
        ss += ii;
        ii = width = space = 0;
      } else {
        free(rr);

        break;
      }

    // backspace?
    } else if (cc=='\b') {
      // Find last set bit, and clear it. This handles wide chars and tabs.
      while (width) {
        --width;
        if (TT.bs[width/8]&(1<<(width&7))) break;
      }
      TT.bs[width/8] &= ~(1<<(width&7));

    // Is it time to wrap?

    } else if (width+ww>TT.w && ss[ii+bb]!='\b'
               && (ii || !strchr("\r\n", ss[ii+bb])))
    {
      if (!ii) ii += bb;
      if (!space) space = ii;

      cc = ss[space];
      ss[space] = '\n';
      xwrite(1, ss, space+1);
      ss += space;
      *ss = cc;
      ii = width = space = 0;

    // move the cursor, recording starting position in bitfield for backspace
    } else {
      TT.bs[width/8] |= (1<<(width&7));
      ii += bb;
      width += ww;
      if (iswspace(cc)) space = ii;
    }
  }
  if (fp != stdin) fclose(fp);
}

void fold_main(void)
{
  TT.bs = xmalloc((TT.w+7)/8);

  loopfiles(toys.optargs, do_fold);
  loopfiles_rw(toys.optargs, O_RDONLY|WARN_ONLY, 0, do_fold);
}
