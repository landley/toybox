/* seq.c - Count from first to last, by increment.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/seq.html

USE_SEQ(NEWTOY(seq, "<1>3?", TOYFLAG_USR|TOYFLAG_BIN))

config SEQ
  bool "seq"
  depends on TOYBOX_FLOAT
  default y
  help
    usage: seq [first] [increment] last

    Count from first to last, by increment. Omitted arguments default
    to 1. Two arguments are used as first and last. Arguments can be
    negative or floating point.
*/

#include "toys.h"

void seq_main(void)
{
  double first, increment, last, dd;

  // Parse command line arguments, with appropriate defaults.
  // Note that any non-numeric arguments are treated as zero.
  first = increment = 1;
  switch (toys.optc) {
    case 3: increment = atof(toys.optargs[1]);
    case 2: first = atof(*toys.optargs);
    default: last = atof(toys.optargs[toys.optc-1]);
  }

  // Yes, we're looping on a double.  Yes rounding errors can accumulate if
  // you use a non-integer increment.  Deal with it.
  for (dd=first; (increment>0 && dd<=last) || (increment<0 && dd>=last);
    dd+=increment)
  {
    printf("%g\n", dd);
  }
}
