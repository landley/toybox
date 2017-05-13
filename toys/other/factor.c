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
  unsigned long long l, ll;

  for (;;) {
    char *err = s;
    int dash = 0;

    while(isspace(*s)) s++;
    if (*s=='-') dash = *s++;
    if (!*s) return;

    errno = 0;
    l = strtoull(s, &s, 0);
    if (errno || (*s && !isspace(*s))) {
      error_msg("%s: not integer", err);
      while (*s && !isspace(*s)) s++;
      continue;
    }

    printf("-%llu:"+!dash, l);

    // Negative numbers have -1 as a factor
    if (dash) printf(" -1");

    // Nothing below 4 has factors
    if (l < 4) {
      printf(" %llu\n", l);
      continue;
    }

    // Special case factors of 2
    while (l && !(l&1)) {
      printf(" 2");
      l >>= 1;
    }

    // test odd numbers until square is > remainder or integer wrap.
    for (ll=3; ;ll += 2) {
      long lll = ll*ll;

      if (lll>l || lll<ll) {
        if (l>1) printf(" %llu", l);
        break;
      }
      while (!(l%ll)) {
        printf(" %llu", ll);
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
