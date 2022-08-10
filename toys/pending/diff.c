/* diff.c - compare files line by line
 *
 * Copyright 2022 Rob Landley <rob@landley.net>
 *
 * See https://pubs.opengroup.org/onlinepubs/9699919799/utilities/diff.html
 *
 * Deviations from posix: only supports unified diff.
 * Ignores -du, uses a simple search algorithm.

USE_DIFF(NEWTOY(diff, "<2>2(unchanged-line-format):;(old-line-format):;(new-line-format):;(color)(strip-trailing-cr)B(ignore-blank-lines)d(minimal)b(ignore-space-change)ut(expand-tabs)w(ignore-all-space)i(ignore-case)T(initial-tab)s(report-identical-files)q(brief)a(text)S(starting-file):L(label)*N(new-file)r(recursive)U(unified)#<0=3", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_ARGFAIL(2)))

config DIFF
  bool "diff"
  default n
  help
  usage: diff [-abBiNqrTstw] [-L LABEL] [-S FILE] [-U LINES] FILE1 FILE2

  Show differences between files in unified diff format.
*/

#define FOR_diff
#include "toys.h"

GLOBALS(
  long U;
  struct arg_list *L;
  char *S, *nou[3];
)

// Array of lines read from an input file, with "unget" support.
struct half_hunk {
  char *name, **str;
  FILE *fp;
  int pos, len, saved, done;
};

// Compare two lines using difference flags
static int diffcmp(char *s1, char *s2)
{
  // TODO: utf8
  for (;*s1 && *s2; s1++, s2++) {
    if (FLAG(w) || (FLAG(b) && isspace(*s1) && isspace(*s2))
        || (FLAG(strip_trailing_cr) && (!strcmp(s1, "\r") || !strcmp(s2,"\r"))))
    {
      while (isspace(*s1)) s1++;
      while (isspace(*s2)) s2++;
      if (!*s1 || !*s2) break;
    }
    if (FLAG(i) && toupper(*s1)!=toupper(*s2)) break;
    else if (*s1!=*s2) break;
  }

  return (*s1>*s2) ? 1 : -(*s1<*s2);
}

// Append next line to hunk (from saved lines or from input file).
// Set "done" if no more lines available to satisfy request.
static void get_line(struct half_hunk *hh)
{
  if (hh->saved) {
    hh->saved--;
    hh->len++;

    return;
  }

  if (hh->fp) {
    if (!(hh->len&31)) hh->str = xrealloc(hh->str, sizeof(void *)*(hh->len+32));
    if (!(hh->str[hh->len] = xgetline(hh->fp))) {
      if (hh->fp != stdin) fclose(hh->fp);
      hh->fp = 0;
    } else hh->len++;
  }

  if (!hh->fp && !hh->saved) hh->done = 1;
}

// Free leading lines from half-hunk, moving remaining data down
static void trim_hunk(struct half_hunk *hh, int keep)
{
  int ii, jj = hh->len-keep;

  if (jj<1) return;
  for (ii = 0; ii<jj; ii++) free(hh->str[ii]);
  memmove(hh->str, hh->str+jj, ((hh->len -= jj)+hh->saved)*sizeof(*hh->str));
  hh->pos += jj;
}

static struct half_hunk *hunky(char *name)
{
  struct half_hunk *new = xzalloc(sizeof(struct half_hunk));

  new->name = name;
  new->fp = !strcmp(name, "-") ? stdin : xfopen(name, "r");
  new->pos++;

  return new;
}

static void dory(struct half_hunk *hh)
{
  int ii;

  for (ii = 0; ii<hh->len+hh->saved; ii++) free(hh->str[ii]);
  free(hh->str);
  if (hh->fp && hh->fp != stdin) fclose(hh->fp);
  free(hh);
}

// Search backward through h2 for U*2 many lines matching end of h1, ignoring
// start many lines in front. Since unified diff hunks can't overlap (first
// line of new hunk can't be within old hunk) any match run shorter than
// end+start can't break hunk. Return length of hunk.
static int back_search(int start, struct half_hunk *h1, struct half_hunk *h2)
{
  int uu = TT.U*2, ii, jj = 0, kk = start+uu;

  // Grab new line.
  get_line(h1);
  if (h1->done || h1->len<kk) return 0;

  // Do we now have TT.U*2 trailing lines of match after common "start"?
  for (ii = h2->len;; ii--)  {
    if (ii<kk) return 0;
    for (jj = 0; jj<uu; jj++)
      if (diffcmp(h1->str[h1->len-jj-1], h2->str[ii-jj-1])) break;
    if (jj == uu) break;
  }

  // unload any extra lines of h2 we searched past to find this match
  if (ii<h2->len) {
    h2->saved += h2->len-ii;
    h2->len = ii;
  }

  return ii;
}

// Is this string slot in this half-hunk?
static int isin(struct half_hunk *h1, char **str)
{
  return str>=h1->str && str<h1->str+h1->len;
}

// to the tune of "zoot suit riot"
static int qsort_callback(char ***a, char ***b)
{
  return diffcmp(**a, **b);
}

static char *squish(char **a)
{
  return (1&*(unsigned long *)a) ? --*a : *a;
}

static void dump_hunk(struct half_hunk *h1, struct half_hunk *h2)
{
  int ii, jj, kk, ll, mm, nn;
  char ***big = xmalloc((kk = h1->len+h2->len)*sizeof(void *));

  // TODO: "quote", label, tab-date
  // TODO: when diff line?
  printf("diff %s %s\n--- %s\n+++ %s\n", h1->name, h2->name, h1->name,h2->name);
  printf("@@ %d,%d %d,%d @@%s\n", h1->pos, h1->len, h2->pos, h2->len, " no()");

  // Collate both hunks and sort to find duplicate lines
  for (ii = 0; ii<h1->len; ii++) big[ii] = h1->str+ii;
  for (ii = 0; ii<h2->len; ii++) big[ii+h1->len] = h2->str+ii;
  qsort(big, kk, sizeof(*big), (void *)qsort_callback);

  // Tag lines repeated on both sides by setting bottom bit of aligned pointer
  for (ii = 1, jj = ll = 0; ii<=kk; ii++) {
    if (ii==kk || diffcmp(*big[ii-1], *big[ii])) {
      if (jj==ii-1 || !ll) jj = ii;
      else while (jj<ii) ++*big[jj++];
      ll = 0;
    } else if (isin(h1, big[ii-1]) != isin(h1, big[ii])) ll++;
  }
  free(big);

  // Loop through lines of output
  for (ii = jj = 0;;) {
    // Find next pair (or end if none)
    for (nn = 0; !nn;) {
      // Skip non-matching lines, then search for this line's match
      for (kk = ii; kk<h1->len && !(1&(unsigned long)h1->str[kk]); kk++);
      for (ll = jj; ll<h2->len && !(1&(unsigned long)h2->str[ll]); ll++);
      if (kk==h1->len && ll==h2->len) return;
      for (mm = 0;; mm++) {
        // Off end?
        if (kk+mm>=h1->len && kk+mm>=h2->len) {
          nn = 0;

          break;
        }

        // next matching line of h1 found in h2?
        if (kk+mm<h1->len && (1&(unsigned long)h1->str[kk+mm])) {
          if (!diffcmp(h1->str[kk+mm]-1, h2->str[ll]-1)) {
            nn = 1;

            break;
          }
        }

        // next matching line of h2 found in h1?
        if (ll+mm<h2->len && (1&(unsigned long)h2->str[ll+mm])) {
          if (!diffcmp(h1->str[kk]-1, h2->str[ll+mm]-1)) {
            nn = 2;

            break;
          }
        }
      }

      // Did we find a match?
      if (!nn) {
        if (kk<h1->len) kk++;
        if (ll<h2->len) ll++;

        continue;
      }
      if (nn==1) kk += mm;
      else ll += mm;
    }
    if (!nn) continue;

    while (ii<kk) printf("-%s\n", squish(h1->str+ii++));
    while (jj<ll) printf("+%s\n", squish(h2->str+jj++));
    if (ii<h1->len) {
      printf(" %s\n", squish(h1->str+ii++));
      squish(h2->str+jj++);
    }
  }
}

// Outputs unified diff hunks to stdout, returns 0 if no differences found
static void find_hunks(struct half_hunk *h1, struct half_hunk *h2)
{
  int start, ii;

  // Search for match.
  for (;;) {
    trim_hunk(h1, TT.U);
    trim_hunk(h2, TT.U);
    start = h1->len;
    get_line(h1);
    get_line(h2);
    if (h1->done && h2->done) break;
    if (h1->done == h2->done)
      if (!diffcmp(h1->str[h1->len-1], h2->str[h2->len-1])) continue;

    // A line did not match! Must emit a diff hunk: find end of hunk.

    // Try to find end of hunk as long as either file has more input.
    // Loop adding line to shorter half-hunk and search backward for TT.U*2
    // matching lines. Hunks can't overlap so first line of new hunk can't be
    // within old hunk so shorter matching run than end+start won't break hunk.
    while (!h1->done || !h2->done) {
      if (!h1->done && h1->len<h2->len) ii = back_search(start, h1, h2);
      else ii = back_search(start, h2, h1);
      if (!ii) continue;

      // Peel off the extra potential start of new hunk at end.
      h1->len -= TT.U;
      h2->len -= TT.U;

      dump_hunk(h1, h2);

      // Reclaim potential start of new hunk
      h1->len += TT.U;
      h2->len += TT.U;

      break;
    }

    // If we ran out of lines, everything left is an unterminated hunk.
    if (h1->done && h2->done) {
      h1->len += h1->saved;
      h2->len += h2->saved;
      h1->saved = h2->saved = 0;

      dump_hunk(h1, h2);

      break;
    }
  }

  dory(h1);
  dory(h2);
}

void diff_main(void)
{
  find_hunks(hunky(toys.optargs[0]), hunky(toys.optargs[1]));
}
