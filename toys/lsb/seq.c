/* seq.c - Count from first to last, by increment.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/seq.html

USE_SEQ(NEWTOY(seq, "<1>3?f:s:w[!fw]", TOYFLAG_USR|TOYFLAG_BIN))

config SEQ
  bool "seq"
  depends on TOYBOX_FLOAT
  default y
  help
    usage: seq [-w|-f fmt_str] [-s sep_str] [first] [increment] last

    Count from first to last, by increment. Omitted arguments default
    to 1. Two arguments are used as first and last. Arguments can be
    negative or floating point.

    -f	Use fmt_str as a printf-style floating point format string
    -s	Use sep_str as separator, default is a newline character
    -w	Pad to equal width with leading zeroes
*/

#define FOR_seq
#include "toys.h"

GLOBALS(
  char *sep;
  char *fmt;
)

// Ensure there's one %f escape with correct attributes
static void insanitize(char *f)
{
  char *s = next_printf(f, 0);

  if (!s) error_exit("bad -f no %%f");
  if (-1 == stridx("aAeEfFgG", *s) || (s = next_printf(s, 0))) {
    // The @ is a byte offset, not utf8 chars. Waiting for somebody to complain.
    error_exit("bad -f '%s'@%d", f, (int)(s-f+1));
  }
}

void seq_main(void)
{
  double first, increment, last, dd;
  char *sep_str = "\n", *fmt_str = "%g";
  int i;

  // Parse command line arguments, with appropriate defaults.
  // Note that any non-numeric arguments are treated as zero.
  first = increment = 1;
  switch (toys.optc) {
    case 3: increment = atof(toys.optargs[1]);
    case 2: first = atof(*toys.optargs);
    default: last = atof(toys.optargs[toys.optc-1]);
  }

  // Pad to largest width
  if (toys.optflags & FLAG_w) {
    char *s;
    int len, dot, left = 0, right = 0;

    for (i=0; i<3; i++) {
      dd = (double []){first, increment, last}[i];

      len = sprintf(toybuf, "%g", dd);
      if ((s = strchr(toybuf, '.'))) {
        dot = s-toybuf;
        if (left<dot) left = dot;
        dot = len-dot-1;
        if (right<dot) right = dot;
      } else if (len>left) left = len;
    }

    sprintf(fmt_str = toybuf, "%%0%d.%df", left+right+!!right, right);
  }
  if (toys.optflags & FLAG_f) insanitize(fmt_str = TT.fmt);
  if (toys.optflags & FLAG_s) sep_str = TT.sep;

  i = 0;
  dd = first;
  if (increment) for (;;) {
    // avoid accumulating rounding errors from increment
    dd = first+i*increment;
    if ((increment<0 && dd<last) || (increment>0 && dd>last)) break;
    if (i++) printf("%s", sep_str);
    printf(fmt_str, dd);
  }

  if (i) printf("\n");
}
