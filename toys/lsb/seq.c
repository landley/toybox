/* seq.c - Count from first to last, by increment.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/seq.html

USE_SEQ(NEWTOY(seq, "<1>3?f:s:", TOYFLAG_USR|TOYFLAG_BIN))

config SEQ
  bool "seq"
  depends on TOYBOX_FLOAT
  default y
  help
    usage: seq [-f fmt_str] [-s sep_str] [first] [increment] last

    Count from first to last, by increment. Omitted arguments default
    to 1. Two arguments are used as first and last. Arguments can be
    negative or floating point.

    -f	Use fmt_str as a printf-style floating point format string
    -s	Use sep_str as separator, default is a newline character
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
  char *s;
  int found = 0;

  for (s = f; *s; s++) {
    if (*s != '%') continue;
    if (*++s == '%') continue;
    if (found++) break;
    while (0 <= stridx("0'#-+ ", *s)) s++;
    while (isdigit(*s)) s++;
    if (*s == '.') s++;
    while (isdigit(*s)) s++;
    if (-1 == stridx("aAeEfFgG", *s)) break;
  }

  // The @ is a byte offset, not utf8 chars. Waiting for somebody to complain...
  if (*s || !found) error_exit("bad -f '%s'@%ld", f, s-f+1);
}

void seq_main(void)
{
  double first, increment, last, dd;
  char *sep_str = "\n";
  char *fmt_str = "%g";
  int output = 0;

  // Parse command line arguments, with appropriate defaults.
  // Note that any non-numeric arguments are treated as zero.
  first = increment = 1;
  switch (toys.optc) {
    case 3: increment = atof(toys.optargs[1]);
    case 2: first = atof(*toys.optargs);
    default: last = atof(toys.optargs[toys.optc-1]);
  }

  if (toys.optflags & FLAG_f) insanitize(fmt_str = TT.fmt);
  if (toys.optflags & FLAG_s) sep_str = TT.sep;

  // Yes, we're looping on a double.  Yes rounding errors can accumulate if
  // you use a non-integer increment.  Deal with it.
  for (dd=first; (increment>0 && dd<=last) || (increment<0 && dd>=last);
    dd+=increment)
  {
    if (dd != first) printf("%s", sep_str);
    printf(fmt_str, dd);
    output = 1;
  }

  if (output) printf("\n");
}
