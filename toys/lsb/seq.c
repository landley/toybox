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

  int precision;
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

// Parse a numeric argument setting *prec to the precision of this argument.
// This reproduces the "1.234e5" precision bug from upstream.
static double parsef(char *s)
{
  char *dp = strchr(s, '.');

  if (dp++) TT.precision = maxof(TT.precision, strcspn(dp, "eE"));

  return xstrtod(s);
}

void seq_main(void)
{
  double first = 1, increment = 1, last, dd;
  int i;

  if (!TT.sep) TT.sep = "\n";
  switch (toys.optc) {
    case 3: increment = parsef(toys.optargs[1]);
    case 2: first = parsef(*toys.optargs);
    default: last = parsef(toys.optargs[toys.optc-1]);
  }

  // Prepare format string with appropriate precision. Can't use %g because 1e6
  if (toys.optflags & FLAG_f) insanitize(TT.fmt);
  else sprintf(TT.fmt = toybuf, "%%.%df", TT.precision);

  // Pad to largest width
  if (toys.optflags & FLAG_w) {
    int len = 0;

    for (i=0; i<3; i++) {
      dd = (double []){first, increment, last}[i];
      len = maxof(len, snprintf(0, 0, TT.fmt, dd));
    }
    sprintf(TT.fmt = toybuf, "%%0%d.%df", len, TT.precision);
  }

  // Other implementations output nothing if increment is 0 and first > last,
  // but loop forever if first < last or even first == last. We output
  // nothing for all three, if you want endless output use "yes".
  if (!increment) return;

  i = 0;
  for (;;) {
    // Multiply to avoid accumulating rounding errors from increment.
    dd = first+i*increment;
    if ((increment<0 && dd<last) || (increment>0 && dd>last)) break;
    if (i++) printf("%s", TT.sep);
    printf(TT.fmt, dd);
  }

  if (i) printf("\n");
}
