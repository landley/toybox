/* factor.c - Factor integers
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * No standard, but it's in coreutils

USE_FACTOR(NEWTOY(factor, 0, TOYFLAG_USR|TOYFLAG_BIN))

config FACTOR
  bool "factor"
  default y
  help
    usage: factor NUMBER...

    Factor integers.
*/

#include "toys.h"

static void factor(char *s)
{
  long l, ll;

  l = strtol(s, &s, 0);
  if (*s) {
    error_msg("%s: not integer");
    return;
  }

  printf("%ld:", l);

  // Negative numbers have -1 as a factor
  if (l < 0) {
    printf(" -1");
    l *= -1;
  }

  // Deal with 0 and 1 (and 2 since we're here)
  if (l < 3) {
    printf(" %ld\n", l);
    return;
  }

  // Special case factors of 2
  while (l && !(l&1)) {
    printf(" 2");
    l >>= 1;
  }

  // test odd numbers.
  for (ll=3; ;ll += 2) {
    if (ll*ll>l) {
      if (l>1) printf(" %ld", l);
      break;
    }
    while (!(l%ll)) {
      printf(" %ld", ll);
      l /= ll;
    }
  }
  xputc('\n');
}

void factor_main(void)
{
  if (toys.optc) {
    char **ss;

    for (ss = toys.optargs; *ss; ss++) factor(*ss);
  } else for (;;) {
    char *s = 0;
    size_t len = 0;

    if (-1 == getline(&s, &len, stdin)) break;
    factor(s);
  }
}
