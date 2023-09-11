/* tsort.c - topological sort
 *
 * Copyright 2023 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/tsort.html

USE_TSORT(NEWTOY(tsort, ">1", TOYFLAG_USR|TOYFLAG_BIN))

config TSORT
  bool "tsort"
  default n
  help
    usage: tsort [FILE]

    Topological sort: read pairs of input strings indicating before/after
    relationships. Output sorted result if no cycles, or first cycle to stderr.
*/

#include "toys.h"

// sort by second element
static int klatch(char **a, char **b)
{
  return strcmp(a[1], b[1]);
}

// TODO: this treats NUL in input as EOF
static void do_hersheba(int fd, char *name)
{
  off_t plen;
  char *djel = readfd(fd, 0, &plen), *ss, **pair, *keep[2];
  long ii, jj, kk, ll, len = 0, first = 1;

  // Count input entries
  if (!djel) return;
  for (ss = djel;;) {
    while (isspace(*ss)) ss++;
    if (!*ss) break;
    len++;
    while (*ss && !isspace(*ss)) ss++;
  }
  if (len&1) error_exit("bad input (not pairs)");

  // collect entries and null terminate strings
  pair = xmalloc(len*sizeof(char *));
  for (ss = djel, len = 0;;) {
    while (isspace(*ss)) *ss++ = 0;
    if (!*ss) break;
    pair[len++] = ss;
    while (*ss && !isspace(*ss)) ss++;
  }

  // sort by second element
  len /= 2;
  qsort(pair, len, sizeof(keep), (void *)klatch);

  // Pull out depends-on-self nodes, printing non-duplicate orpans
  while (len) {
    for (ii = 0, kk = ll = 2*len; ii<len; ii++) {
      // First time through pull out depends-on-self nodes,
      // else pull out nodes no other node depends on
      if (first ? (long)strcmp(pair[2*ii], pair[2*ii+1])
          : (long)bsearch(pair+2*ii-1, pair, len, sizeof(keep), (void *)klatch))
        continue;

      // Remove node from list, keeping at end for dupe killing
      memcpy(keep, pair+2*ii, sizeof(keep));
      memmove(pair+2*ii, pair+2*(ii+1), (--len-ii)*sizeof(keep));
      ii--;

      // depends-on-self nodes only count if nobody else depends on them
      if (first && bsearch(keep, pair, len, sizeof(keep), (void *)klatch))
        continue;

      // Print non-duplicate nodes we removed
      for (jj = kk; jj<ll; jj++) if (!strcmp(*keep, pair[jj])) break;
      if (jj==ll) xprintf("1) %s\n", pair[--kk] = *keep);
      if (bsearch(keep, pair, len, sizeof(keep), (void *)klatch)) continue;
      for (jj = kk; jj<ll; jj++) if (!strcmp(keep[1], pair[jj])) break;
      if (jj==ll) xprintf("2) %s\n", pair[--kk] = keep[1]);
    }

    // If we removed nothing, break;
    if (first) first = 0;
    else if (ll == 2*len) break;
  }

  // If we couldn't empty list, there's a cycle
  if (len) error_msg("cycle from %s", *pair);
}

void tsort_main(void)
{
  loopfiles(toys.optargs, do_hersheba);
}
