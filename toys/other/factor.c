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

  for (;;) {
    char *err = s;

    while(isspace(*s)) s++;
    if (!*s) return;

    l = strtol(s, &s, 0);
    if (*s && !isspace(*s)) {
      error_msg("%s: not integer", err);

      return;
    }

    printf("%ld:", l);

    // Negative numbers have -1 as a factor
    if (l < 0) {
      printf(" -1");
      l *= -1;
    }

    // Nothing below 4 has factors
    if (l < 4) {
      printf(" %ld\n", l);
      continue;
    }

    // Special case factors of 2
    while (l && !(l&1)) {
      printf(" 2");
      l >>= 1;
    }

    // test odd numbers.
    for (ll=3; ;ll += 2) {
      long lll = ll*ll;

      if (lll>l || lll<ll) {
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
