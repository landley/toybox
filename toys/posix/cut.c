/* cut.c - print selected ranges from a file
 *
 * Copyright 2016 Rob Landley <rob@landley.net>
 *
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/cut.html
 *
 * Deviations from posix: added -DF. We can only accept 512 selections, and
 * "-" counts as start to end. Using spaces to separate a comma-separated list
 * is silly and inconsistent with dd, ps, cp, and mount.
 *
 * todo: -n, -s with -c

USE_CUT(NEWTOY(cut, "b*|c*|f*|F*|C*|O(output-delimiter):d:sDn[!cbf]", TOYFLAG_USR|TOYFLAG_BIN))

config CUT
  bool "cut"
  default y
  help
    usage: cut [-Ds] [-bcfF LIST] [-dO DELIM] [FILE...]

    Print selected parts of lines from each FILE to standard output.

    Each selection LIST is comma separated, either numbers (counting from 1)
    or dash separated ranges (inclusive, with X- meaning to end of line and -X
    from start). By default selection ranges are sorted and collated, use -D
    to prevent that.

    -b	select bytes
    -c	select UTF-8 characters
    -C	select unicode columns
    -d	use DELIM (default is TAB for -f, run of whitespace for -F)
    -D	Don't sort/collate selections
    -f	select fields (words) separated by single DELIM character
    -F	select fields separated by DELIM regex
    -O	output delimiter (default one space for -F, input delim for -f)
    -s	skip lines without delimiters
*/
#define FOR_cut
#include "toys.h"

GLOBALS(
  char *d;
  char *O;
  struct arg_list *select[5]; // we treat them the same, so loop through

  int pairs;
  regex_t reg;
)

// Return number of bytes to start of first column fitting in columns
// invalid sequences are skipped/ignored
int unicolumns(char *start, unsigned columns)
{
  int i, j = 0;
  wchar_t wc;
  char *s = start, *ss = start;

  // Skip start, rounding down if we hit a multicolumn char
  while (j<columns && (i = utf8towc(&wc, s, 4))) {
    if (i<0) s++;
    else {
      s += i;
      if (0<(i = wcwidth(wc))) {
        if ((j += i)>columns) break;
        ss = s;
      }
    }
  }

  return ss-start;
}


// Apply selections to an input line, producing output
static void cut_line(char **pline, long len)
{
  unsigned *pairs = (void *)toybuf;
  char *line = *pline;
  int i, j;

  if (len && line[len-1]=='\n') line[--len] = 0;

  // Loop through selections
  for (i=0; i<TT.pairs; i++) {
    unsigned start = pairs[2*i], end = pairs[(2*i)+1], count;
    char *s = line, *ss;

    // input: start/end position, count=difference between them
    // output: s = start of string, len = bytes to output

    if (start) start--;
    if (start>=len) continue;
    if (!end || end>len) end = len;
    count = end-start;

    // Find start and end of output string for the relevant selection type
    if (toys.optflags&FLAG_b) s += start;
    else if (toys.optflags&FLAG_C) {
      // crunch_str() currently assumes that combining characters get
      // escaped, to provide an unambiguous visual representation.
      // This assumes the input string is null terminated.
      //if (start) crunch_str(&s, start, 0, 0, 0);
      //if (!*s) continue;
      //start = s-line;
      //ss = s;
      //crunch_str(&ss, count, 0, 0, 0);
      //count = ss-s;

      s += unicolumns(s, start);
      count = unicolumns(s, end-start);
    } else if (toys.optflags&FLAG_c) {
      wchar_t wc;
      char *sss;

      // Find start
      ss = line+len;
      while (start && s<ss) {
        if (0<=(j = utf8towc(&wc, s, len))) start--;
        s += (j<1) ? 1 : j;
      }
      if (s == ss) continue;

      // Find end
      end = count;
      sss = s;
      while (end && sss<ss) {
        if (0<=(j = utf8towc(&wc, sss, len))) end--;
        sss += (j<1) ? 1 : j;
      }
      count = sss-s;
    } else {
      regmatch_t match;

      // Loop through skipping appropriate number of fields
      for (j = 0; j<2; j++) {
        ss = s;
        if (j) start = count;
        else end = start;
        while (*ss && start) {
          if (toys.optflags&FLAG_f) {
            if (!strchr(TT.d, *ss++)) continue;
            if (!--start && j) ss--;
          } else {
            if (regexec(&TT.reg, ss, 1, &match, REG_NOTBOL|REG_NOTEOL)) {
              ss = line+len;
              continue;
            }
            if (!match.rm_eo) break; // zero length match == no delimiter
            ss += (!--start && j) ? match.rm_so : match.rm_eo;
          }
        }
        if (!j && !*(s = ss)) break;
      }

      // If we never encountered even one separator, print whole line (posix!)
      if (!j && end == start) {
        if (toys.optflags&FLAG_s) return;
        fwrite(line, len, 1, stdout);
        break;
      } else if (!*s) continue;
      count = ss-s;
    }
    if (i && TT.O) fputs(TT.O, stdout);
    fwrite(s, count, 1, stdout);
  }
  xputc('\n');
}

static int compar(unsigned *a, unsigned *b)
{
  if (*a<*b) return -1;
  if (*a>*b) return 1;
  if (a[1]<b[1]) return -1;
  if (a[1]>b[1]) return 1;

  return 0;
}

// parse A or A-B or A- or -B
static char *get_range(void *data, char *str, int len)
{
  char *end = str;
  unsigned *pairs = (void *)toybuf, i;

  // Using toybuf[] to store ranges means we can have 512 selections max.
  if (TT.pairs == sizeof(toybuf)/sizeof(int)) perror_exit("select limit");
  pairs += 2*TT.pairs++;

  pairs[1] = UINT_MAX;
  for (i = 0; ;i++) {
    if (i==2) return end;
    if (isdigit(*end)) {
      long long ll = estrtol(end, &end, 10);

      if (ll<1 || ll>UINT_MAX || errno) return end;
      pairs[i] = ll;
    }
    if (*end++ != '-') break;
  }
  if (!i) pairs[1] = pairs[0];
  if ((end-str)<len) return end;
  if (pairs[0]>pairs[1]) return str;

  // No error
  return 0;
}

void cut_main(void)
{
  int i;
  char buf[8];

  // Parse command line arguments
  if ((toys.optflags&(FLAG_s|FLAG_f|FLAG_F))==FLAG_s)
    error_exit("-s needs -Ff");
  if ((toys.optflags&(FLAG_d|FLAG_f|FLAG_F))==FLAG_d)
    error_exit("-d needs -Ff");
  if (!TT.d) TT.d = (toys.optflags&FLAG_F) ? "[[:space:]][[:space:]]*" : "\t";
  if (toys.optflags&FLAG_F) xregcomp(&TT.reg, TT.d, REG_EXTENDED);
  if (!TT.O) {
    if (toys.optflags&FLAG_F) TT.O = " ";
    else if (toys.optflags&FLAG_f) TT.O = TT.d;
  }

  // Parse ranges, which are attached to a selection type (only one can be set)
  for (i = 0; i<ARRAY_LEN(TT.select); i++) {
    sprintf(buf, "bad -%c", "CFfcb"[i]); // reverse order from newtoy optstr
    if (TT.select[i]) comma_args(TT.select[i], 0, buf, get_range);
  }
  if (!TT.pairs) error_exit("no selections");

  // Sort and collate selections
  if (!(toys.optflags&FLAG_D)) {
    int from, to;
    unsigned *pairs = (void *)toybuf;

    qsort(toybuf, TT.pairs, 8, (void *)compar);
    for (to = 0, from = 2; from/2 < TT.pairs; from += 2) {
      if (pairs[from] > pairs[to+1]) {
        to += 2;
        memcpy(pairs+to, pairs+from, 2*sizeof(unsigned));
      } else if (pairs[from+1] > pairs[to+1]) pairs[to+1] = pairs[from+1];
    }
    TT.pairs = (to/2)+1;
  }

  // For each argument, loop through lines of file and call cut_line() on each
  loopfiles_lines(toys.optargs, cut_line);
}
