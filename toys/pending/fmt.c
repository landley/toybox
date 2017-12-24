/* fmt.c - Text formatter
 *
 * Copyright 2017 The Android Open Source Project
 *
 * Deviations from original:
 *   we treat all whitespace as equal (no tab expansion, no runs of spaces)
 *   we don't try to recognize ends of sentences to double-space after ./?/!

USE_FMT(NEWTOY(fmt, "w#", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_LOCALE))

config FMT
  bool "fmt"
  default y
  help
    usage: fmt [-w WIDTH] [FILE...]

    Reformat input to not exceed a maximum line length.

    -w WIDTH	maximum characters per line (default 75)
*/

#define FOR_fmt
#include "toys.h"

GLOBALS(
  int width;
)

static void do_fmt(int fd, char *name)
{
  FILE *fp = xfdopen(fd, "re");
  char *line = NULL;
  size_t allocated_length = 0;
  int cols = 0, is_first = 1, indent_end = 0, line_length;

  while ((line_length = getline(&line, &allocated_length, fp)) > 0) {
    int b = 0, e, w;

    while (b < line_length && isspace(line[b])) b++;
    if (b == line_length) {
      if (cols > 0) xputc('\n');
      xputc('\n');
      is_first = 1;
      cols = 0;
      continue;
    }
    if (is_first) indent_end = b;

    for (; b < line_length; b = e + 1) {
      while (isspace(line[b])) b++;
      for (e = b + 1; e < line_length && !isspace(line[e]);) e++;
      if (e >= line_length) break;

      line[e] = 0;
      w = utf8len(line + b);

      if (!is_first && (cols + (is_first?indent_end:1) + w) >= TT.width) {
        xputc('\n');
        is_first = 1;
        cols = 0;
      }
      xprintf("%.*s%.*s",is_first?indent_end:1,is_first?line:" ",(e-b),line+b);
      cols += (is_first?indent_end:1) + w;
      b = e + 1;
      is_first = 0;
    }
  }
  if (cols > 0) xputc('\n');
  fclose(fp);
}

void fmt_main(void)
{
  if (TT.width < 0) error_exit("negative width: %d", TT.width);
  if (!TT.width) TT.width = 75;

  loopfiles(toys.optargs, do_fmt);
}
