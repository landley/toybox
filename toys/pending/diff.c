/* diff.c - compare files line by line
 *
 * Copyright 2022 Rob Landley <rob@landley.net>
 *
 * See https://pubs.opengroup.org/onlinepubs/9699919799/utilities/diff.html
 *
 * Deviations from posix: only supports unified diff.
 * Ignores -du, uses a simple search algorithm.
 * We don't reset -U value for -u (I.E. "-U 1 -u" is 1, not 3.)
 * TODO: -t needs to be UTF8 aware
 * TODO: -pF --show-function-line=RE

USE_DIFF(NEWTOY(diff, "<1>2(unchanged-line-format):;(old-line-format):;(new-line-format):;(color):;(to-file):(from-file):(strip-trailing-cr)B(ignore-blank-lines)d(minimal)b(ignore-space-change)ut(expand-tabs)w(ignore-all-space)i(ignore-case)T(initial-tab)s(report-identical-files)q(brief)a(text)pF(show-function-line):S(starting-file):L(label)*N(new-file)r(recursive)U(unified)#<0=3", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_ARGFAIL(2)))

config DIFF
  bool "diff"
  default n
  help
  usage: diff [-abBiNpqrTstw] [--from-file=FROM] [--to-file=TO] [-F REGEX] [-L LABEL] [-S FILE] [-U LINES] FILE...

  Show differences between files in unified diff format. Exit 1 if differences
  found, 0 if no differences, 2 on error.

  Takes two arguments unless --from-file or --to-file specified. If one argument
  is a directory, looks for file of the same name in that directory. If both
  arguments are directories, compare contents of directories (in alphabetical
  order).

  -F	Show last line matching REGEX
  -p	Show last unindented line before each change
  -q	Quiet (don't display differences, just say files differ)
  -S	Skip every filename in directory before FILE
  -r	Recurse into subdirectories

  --from-file=FROM	Compare all arguments against old file FROM
  --to-file=TO	Compare all arguments against new file TO

  --color     Color output   --strip-trailing-cr   Strip '\r' from input lines
  --TYPE-line-format=FORMAT  Display TYPE (unchanged/old/new) lines using FORMAT
    FORMAT uses printf integer escapes (ala %-2.4x) followed by LETTER: FELMNn
  Supported format specifiers are:
  * %l, the contents of the line, without the trailing newline
  * %L, the contents of the line, including the trailing newline
  * %%, the character '%'
*/

#define FOR_diff
#include "toys.h"

GLOBALS(
  long U;
  struct arg_list *L;
  char *S, *from_file, *to_file, *color, *nou[3];

  int sawdiff, newfile;
)

// Array of lines read from an input file, with "unget" support.
struct half_hunk {
  char *name, *funky, **str;
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
  for (ii = 0; ii<jj; ii++) {
    char *ss = hh->str[ii];

    if (FLAG(p) && *ss && !isspace(*ss)) {
      free(hh->funky);
      hh->funky = ss;
    } else free(ss);
  }
  memmove(hh->str, hh->str+jj, ((hh->len -= jj)+hh->saved)*sizeof(*hh->str));
  hh->pos += jj;
}

static struct half_hunk *hunky(char *name)
{
  FILE *fp = !strcmp(name, "-") ? stdin : fopen(name, "r");
  struct half_hunk *new = 0;

  if (!fp) perror_msg("Bad %s", name);
  else {
    new = xzalloc(sizeof(struct half_hunk));
    new->name = name;
    new->fp = fp;
    new->pos++;
  }

  return new;
}

static void dory(struct half_hunk *hh)
{
  int ii;

  if (hh) {
    for (ii = 0; ii<hh->len+hh->saved; ii++) free(hh->str[ii]);
    free(hh->str);
    if (hh->fp && hh->fp != stdin) fclose(hh->fp);
  }
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
static int qsort_hunk_callback(char ***a, char ***b)
{
  return diffcmp(**a, **b);
}

// Re-align marked pointers (detected as duplicates).
static char *squish(char **a)
{
  return (1&*(unsigned long *)a) ? --*a : *a;
}

static void dump_hunk(struct half_hunk *h1, struct half_hunk *h2)
{
  int ii, jj, kk, ll, mm;
  char *ss = toybuf, ***big;

  if (FLAG(B)) {
    for (ii = 0; ii<h1->len; ii++) if (!isspace(h1->str[ii])) break;
    if (ii==h1->len) {
      for (ii = 0; ii<h2->len; ii++) if (!isspace(h2->str[ii])) break;
      if (ii==h2->len) return;
    }
  }

  toys.exitval = 1;
  if (FLAG(q)) return;

// TODO: test from len 1 and to len 1 so ,%d drops out
// TODO: test 0 if empty range starts the file

  if (TT.newfile) {
    // TODO: "quote", --label, tab-date
    // TODO: when do we output this line?
    xprintf("diff %s %s\n--- %s\n+++ %s\n", h1->name, h2->name, h1->name,
            h2->name);
  }
  ss += sprintf(ss, "-%d", h1->pos);
  if (h1->len>1) ss += sprintf(ss, ",%d", h1->len);
  ss += sprintf(ss, " +%d", h2->pos);
  if (h2->len>1) ss += sprintf(ss, ",%d", h2->len);
  xprintf("@@ %s @@%s\n", toybuf, ""); //" no()");
  TT.newfile = 0;

  // Collate both hunks and sort to find duplicate lines

  big = xmalloc((kk = h1->len+h2->len)*sizeof(void *));
  for (ii = 0; ii<h1->len; ii++) big[ii] = h1->str+ii;
  for (ii = 0; ii<h2->len; ii++) big[ii+h1->len] = h2->str+ii;
  qsort(big, kk, sizeof(*big), (void *)qsort_hunk_callback);

  // Tag lines repeated on both sides by setting bottom bit of aligned pointer
  for (ii = 1, jj = ll = 0; ii<=kk; ii++) {
    if (ii==kk || diffcmp(*big[ii-1], *big[ii])) {
      if (jj==ii-1 || !ll) jj = ii;
      else while (jj<ii) ++*big[jj++];
      ll = 0;
    } else if (isin(h1, big[ii-1]) != isin(h1, big[ii])) ll++;
  }
  free(big);

  // Loop through lines of output looking for next closest matching line
  for (ii = jj = kk = ll = 0; ii<h1->len || ll<h2->len;) {
    // Skip non-matching (untagged) lines on each side, which must be - or +
    while (kk<h1->len && !(1&(unsigned long)h1->str[kk])) kk++;
    while (ll<h2->len && !(1&(unsigned long)h2->str[ll])) ll++;

    // Have we hit the end? (Can happen at EOF even with -U >0)
    if (kk==h1->len || ll==h2->len) {
      kk = h1->len;
      ll = h2->len;

    // Loop until either side finds a match across the aisle (shortest offset
    // to next match) or falls off end (match here).
    } else for (mm = 0;;mm++) {
      if (kk+mm<h1->len && ll+mm<h2->len) {
        // Next line of h1 matched in h2?
        if ((1&(unsigned long)h1->str[kk+mm])
          && !diffcmp(h1->str[kk+mm]-1, h2->str[ll]-1)) kk += mm;

        // Next line of h2 matched in h1?
        else if ((1&(unsigned long)h2->str[ll+mm])
          && !diffcmp(h1->str[kk]-1, h2->str[ll+mm]-1)) ll += mm;
        else continue;

        break;
      }

      // If we fell off end, line on other side did not match.
      if (kk+mm>=h1->len) squish(h1->str+ll++);
      if (ll+mm>=h2->len) squish(h2->str+kk++);

      goto loop2;
    }

    while (ii<kk) xprintf("-%s\n", squish(h1->str+ii++));
    while (jj<ll) xprintf("+%s\n", squish(h2->str+jj++));
    if (ii<h1->len) {
      xprintf(" %s\n", squish(h1->str+ii++));
      squish(h2->str+jj++);
    }
loop2:
    ; // gcc complains without this semicolon. Really!
  }
}

// Outputs unified diff hunks to stdout, returns 0 if no differences found
static int find_hunks(struct half_hunk *h1, struct half_hunk *h2)
{
  int start, ii, differ = 0;

  // Search for match.
  TT.newfile = 1;
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
      if (!h1->done && (h2->done || h1->len<h2->len))
        ii = back_search(start, h1, h2);
      else ii = back_search(start, h2, h1);
      if (!ii) continue;

      // Peel off the extra (potential start of new hunk at end).
      h1->len -= TT.U;
      h2->len -= TT.U;

      differ = 1;
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

      differ = TT.sawdiff = 1;
      dump_hunk(h1, h2);

      break;
    }
  }

  return differ;
}

// This plumbing exists because dirtree() isn't really set up to descend
// two directories in parallel. (Even cp/mv don't readdir(to), just from.)

struct dir_entry {
  struct dir_entry *next;
  struct stat st;
  char name[];
};

struct dir_pair {
  struct dir_pair *next;
  int fd[2];
  unsigned len[2];
  struct dir_entry **dd[2];
};

static struct dir_entry *get_de(int dirfd, char *name)
{
  struct dir_entry *de = 0;
  struct stat st;

  if (fstatat(dirfd, name, &st, 0)) perror_msg("%s", name);
  else {
    de = xmalloc(sizeof(*de)+strlen(name)+1);
    memcpy(&de->st, &st, sizeof(st));
    strcpy(de->name, name);
    de->next = 0;
  }

  return de;
}

static struct dir_pair *pop_dir_pair(struct dir_pair *dp)
{
  struct dir_pair *next = dp->next;
  unsigned ii, uu;

  for (ii = 0; ii<2; ii++) {
    if (dp->fd[ii]>2) close(dp->fd[ii]);
    for (uu = 0; uu<dp->len[ii]; uu++) free(dp->dd[ii][uu]);
  }
  free(dp);

  return next;
}

// traverse dir_pair stack to make malloced path string, and return it
static char *connect_name(struct dir_pair *dp, int side)
{
  char *s = 0, *ss;
  unsigned len = 0, jj;
  struct dir_pair *dd;

  for (jj = 0; jj<2; jj++) {
    if (jj) s = xmalloc(len)+len;
    for (dd = dp; dd; dd = dd->next) {
      int ii = strlen(ss = dd->dd[side][dd->len[side]]->name);

      while (ii>1 && ss[ii-1]=='/') ii--;
      if (!s) len += ii+1;
      else {
        *--s = '/'*(dd!=dp);
        memcpy(s -= ii, ss, ii);
      }
    }
  }

  return s;
}

// Compare two names which could be file+file, file+dir, or dir+dir
void do_diff(char *from, char *to)
{
  struct dir_pair *dp = xzalloc(sizeof(struct dir_pair)+2*sizeof(void *));
  struct dir_entry *de1, *de2;
  char *types[] = {"Fifo", "Char device", "Directory", "Block device",
                   "Regular file", "Symbolic link", "Socket", "Potato"},
       *s1, *s2;
  int ii, mode;

  // Setup dir_pair for initial arguments so same loop can handle all cases
  dp->len[0] = dp->len[1] = 1;
  dp->dd[1] = (dp->dd[0] = (void *)(dp+1))+1;
  *dp->dd[0] = get_de(dp->fd[0] = AT_FDCWD, from);
  *dp->dd[1] = get_de(dp->fd[1] = AT_FDCWD, to);
  if (!*dp->dd[0] || !*dp->dd[1]) goto done;


  // Are we comparing dir to file at top level?
  if (S_ISDIR(dp->dd[0][0]->st.st_mode)
      != (ii = S_ISDIR(dp->dd[1][0]->st.st_mode)))
  {
    free(*dp->dd[ii]);
    s1 = ii ? to : from;
    s1 = xmprintf("%s%s%s", s1, (s1[1] && s1[strlen(s1)-1]!='/') ? "/" : "",
      getbasename(ii ? from : to));
    *dp->dd[ii] = get_de(AT_FDCWD, s1);
    free(s1);
    if (!*dp->dd[ii]) goto done;
  }

  // Loop through matching dir_entry pairs in this dir_pair
  while (dp) {
    while (dp->len[0] || dp->len[1]) {
      mode = 0;

      // Do we have unmatched names?
      if (!dp->len[0]) ii = 1;
      else if (!dp->len[1]) ii = -1;
      else if (!dp->next) ii = 0;
      else ii = strcmp(dp->dd[0][dp->len[0]-1]->name, dp->dd[1][dp->len[1]-1]->name);

      if (ii) {
        dp->len[ii>0]--;
        s1 = connect_name(dp->next, ii>0);
        if (FLAG(N)) {
          s2 = xstrdup("/dev/null");
          if (ii>0) {
            char *temp = s2;

            s2 = s1;
            s1 = temp;
          }

          goto missing;
        }
        xprintf("Only in %s: %s\n", s1, dp->dd[ii>0][dp->len[ii>0]]->name);
        free(s1);

        continue;
      }

      // Are these identical files?
      de1 = dp->dd[0][--dp->len[0]];
      de2 = dp->dd[1][--dp->len[1]];
      if (same_file(&de1->st, &de2->st)) goto same;

      // Are they comparable types?
      if (!(((mode = de1->st.st_mode)^de2->st.st_mode)&S_IFMT)) {
        if (S_ISBLK(mode) || S_ISCHR(mode))
          if (de1->st.st_rdev == de2->st.st_rdev) goto same;
        if (S_ISDIR(mode)) {
          if (FLAG(r) || !dp->next) {
dprintf(2, "TODO: read subdirs and add dp\n");
//      qsort(ll+1, *ll, sizeof(long), (void *)qsort_dt_callback);
// and detect loops
          } else {
            printf("Common subdirectories:");
            for (ii = 0; ii<2; ii++) {
              printf(" and %s"+4*!ii, s1 = connect_name(dp, ii));
              free(s1);
            }
            xputc('\n');
          }

          continue;
        }
        if (S_ISREG(mode) || !dp->next) {
          struct half_hunk *h1, *h2;

          mode = 0;
same:
          s1 = connect_name(dp, 0);
          s2 = connect_name(dp, 1);
missing:
          if (!mode) {
            h1 = hunky(s1);
            h2 = hunky(s2);
            if (h1 && h2) ii = find_hunks(h1, h2);
            dory(h1);
            dory(h2);
          }
          if (FLAG(s) && !ii)
            xprintf("Files %s and %s are identical\n", s1, s2);
          free(s1);
          free(s2);

          continue;
        }
      }

      // Report incompatibility
      for (ii = 0; ii<2; ii++) {
        printf(" while File %s is a %s"+7*!ii, s1 = connect_name(dp, ii),
          types[(((ii ? de2->st.st_mode : de2->st.st_mode)>>12)&15)>>1]);
        free(s1);
      }
      xputc('\n');

// Handle -N
    }
    dp = pop_dir_pair(dp);
  }


done:
  while (dp) dp = pop_dir_pair(dp);
}

void diff_main(void)
{
  char *ss;
  int ii;

/* requirements:
   dir + dir:
     don't compare block/char/fifo/dir to file
       - skip same dev/ino or same block/char major minor
     "Only in %s: %s\n", <directory pathname>, <filename>
     "Common subdirectories: %s and %s\n", <directory1 pathname>,
        <directory2 pathname>
     "diff %s %s %s\n", <diff_options>, <filename1>, <filename2>
     binary files '%s' and '%s' differ
     with -r descend into subdirs, but detect loops
   file + dir: compare file to dir/file

   timestamp: +%Y-%m-%d%H:%M:%S.000 shhmm

  // TODO: -r and diff file dir
*/

  if (TT.from_file || TT.to_file) {
    if (TT.from_file && TT.to_file) error_exit("no");
    if (access(ss = TT.from_file ? : TT.to_file, F_OK)) perror_exit("'%s'", ss);
    for (ii = 0; ii<toys.optc; ii++)
      do_diff(TT.from_file?:toys.optargs[ii], TT.to_file?:toys.optargs[ii]);
  } else {
    if (toys.optc!=2) error_exit("needs 2 args");
    do_diff(toys.optargs[0], toys.optargs[1]);
  }

/*
stat1, stat2
detect bin file? (Unless -a?)
dir and notdir - append filename to notdir
dir and dir: iterate through top level contents of each
  Only in XXX: XXX
  File X is a XXX while X is a XXX
*/

//  find_hunks(hunky(toys.optargs[0]), hunky(toys.optargs[1]));

  if (!toys.exitval && TT.sawdiff) toys.exitval++;
}
