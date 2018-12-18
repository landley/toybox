/* grep.c - print lines what match given regular expression
 *
 * Copyright 2013 CE Strake <strake888 at gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/grep.html
 *
 * TODO: --color
 *
 * Posix doesn't even specify -r, documenting deviations from it is silly.

USE_GREP(NEWTOY(grep, "S(exclude)*M(include)*ZzEFHIabhinorsvwclqe*f*C#B#A#m#x[!wx][!EFw]", TOYFLAG_BIN))
USE_EGREP(OLDTOY(egrep, grep, TOYFLAG_BIN))
USE_FGREP(OLDTOY(fgrep, grep, TOYFLAG_BIN))

config GREP
  bool "grep"
  default y
  help
    usage: grep [-EFrivwcloqsHbhn] [-ABC NUM] [-m MAX] [-e REGEX]... [-MS PATTERN]... [-f REGFILE] [FILE]...

    Show lines matching regular expressions. If no -e, first argument is
    regular expression to match. With no files (or "-" filename) read stdin.
    Returns 0 if matched, 1 if no match found.

    -e  Regex to match. (May be repeated.)
    -f  File listing regular expressions to match.

    file search:
    -r  Recurse into subdirectories (defaults FILE to ".")
    -M  Match filename pattern (--include)
    -S  Skip filename pattern (--exclude)
    -I  Ignore binary files

    match type:
    -A  Show NUM lines after     -B  Show NUM lines before match
    -C  NUM lines context (A+B)  -E  extended regex syntax
    -F  fixed (literal match)    -a  always text (not binary)
    -i  case insensitive         -m  match MAX many lines
    -v  invert match             -w  whole word (implies -E)
    -x  whole line               -z  input NUL terminated

    display modes: (default: matched line)
    -c  count of matching lines  -l  show matching filenames
    -o  only matching part       -q  quiet (errors only)
    -s  silent (no error msg)    -Z  output NUL terminated

    output prefix (default: filename if checking more than 1 file)
    -H  force filename           -b  byte offset of match
    -h  hide filename            -n  line number of match

config EGREP
  bool
  default y
  depends on GREP

config FGREP
  bool
  default y
  depends on GREP
*/

#define FOR_grep
#include "toys.h"
#include <regex.h>

GLOBALS(
  long m, A, B, C;
  struct arg_list *f, *e, *M, *S;

  struct double_list *reg;
  char indelim, outdelim;
  int found, tried;
)

struct reg {
  struct reg *next, *prev;
  regex_t r;
  int rc;
  regmatch_t m;
};

// Emit line with various potential prefixes and delimiter
static void outline(char *line, char dash, char *name, long lcount, long bcount,
  int trim)
{
  if (name && (toys.optflags&FLAG_H)) printf("%s%c", name, dash);
  if (!line || (lcount && (toys.optflags&FLAG_n)))
    printf("%ld%c", lcount, line ? dash : TT.outdelim);
  if (bcount && (toys.optflags&FLAG_b)) printf("%ld%c", bcount-1, dash);
  if (line) {
    // support embedded NUL bytes in output
    fwrite(line, 1, trim, stdout);
    xputc(TT.outdelim);
  }
}

// Show matches in one file
static void do_grep(int fd, char *name)
{
  long lcount = 0, mcount = 0, offset = 0, after = 0, before = 0;
  struct double_list *dlb = 0;
  char *bars = 0;
  FILE *file;
  int bin = 0;

  TT.tried++;
  if (!fd) name = "(standard input)";

  // Only run binary file check on lseekable files.
  if (!(toys.optflags&FLAG_a) && !lseek(fd, 0, SEEK_CUR)) {
    char buf[256];
    int len, i = 0;
    wchar_t wc;

    // If the first 256 bytes don't parse as utf8, call it binary.
    if (0<(len = read(fd, buf, 256))) {
      lseek(fd, -len, SEEK_CUR);
      while (i<len) {
        bin = utf8towc(&wc, buf+i, len-i);
        if (bin == -2) i = len;
        if (bin<1) break;
        i += bin;
      }
      bin = i!=len;
    }
    if (bin && (toys.optflags&FLAG_I)) return;
  }

  if (!(file = fdopen(fd, "r"))) return perror_msg("%s", name);

  // Loop through lines of input
  for (;;) {
    char *line = 0, *start;
    regmatch_t *mm = (void *)toybuf;
    struct reg *shoe;
    size_t ulen;
    long len;
    int matched = 0, baseline = 0;

    // get next line, check and trim delimiter
    lcount++;
    errno = 0;
    ulen = len = getdelim(&line, &ulen, TT.indelim, file);
    if (errno) perror_msg("%s", name);
    if (len<1) break;
    if (line[ulen-1] == TT.indelim) line[--ulen] = 0;

    // Prepare for next line
    start = line;
    if (TT.reg) for (shoe = (void *)TT.reg;;) {
      shoe->rc = 0;
      if ((shoe = shoe->next) == (void *)TT.reg) break;
    }

    // Loop to handle multiple matches in same line
    do {
      int rc, skip = 0;

      // Handle "fixed" (literal) matches
      if (FLAG(F)) {
        struct arg_list *seek, fseek;
        char *s = 0;

        for (seek = TT.e; seek; seek = seek->next) {
          if (toys.optflags & FLAG_x) {
            int i = (toys.optflags & FLAG_i);

            if ((i ? strcasecmp : strcmp)(seek->arg, line)) s = line;
          } else if (!*seek->arg) {
            seek = &fseek;
            fseek.arg = s = line;
            break;
          }
          if (toys.optflags & FLAG_i) s = strcasestr(line, seek->arg);
          else s = strstr(line, seek->arg);
          if (s) break;
        }

        if (s) {
          rc = 0;
          mm->rm_so = (s-line);
          skip = mm->rm_eo = (s-line)+strlen(seek->arg);
        } else rc = 1;

      // Handle regex matches
      } else {
        mm->rm_so = mm->rm_eo = 0;
        rc = 1;
        for (shoe = (void *)TT.reg;;) {

          // Do we need to re-check this regex?
          if (!shoe->rc && (!matched || (shoe->m.rm_so -= baseline)<0))
            shoe->rc = regexec0(&shoe->r, start, ulen-(start-line), 1,
                                &shoe->m, start==line ? 0 : REG_NOTBOL);

          // If we got a match, is it a _better_ match?
          if (!shoe->rc && (mm->rm_so < shoe->m.rm_so ||
              (mm->rm_so == shoe->m.rm_so && shoe->m.rm_eo >= skip)))
          {
            mm = &shoe->m;
            skip = mm->rm_eo;
            rc = 0;
          }
          if ((shoe = shoe->next) == (void *)TT.reg) break;
        }
        baseline = skip;
      }

      if (!rc && FLAG(x))
        if (mm->rm_so || line[mm->rm_eo]) rc = 1;

      if (!rc && FLAG(w)) {
        char c = 0;

        if ((start+mm->rm_so)!=line) {
          c = start[mm->rm_so-1];
          if (!isalnum(c) && c != '_') c = 0;
        }
        if (!c) {
          c = start[mm->rm_eo];
          if (!isalnum(c) && c != '_') c = 0;
        }
        if (c) {
          start += mm->rm_so+1;

          continue;
        }
      }

      if (toys.optflags & FLAG_v) {
        if (toys.optflags & FLAG_o) {
          if (rc) skip = mm->rm_eo = strlen(start);
          else if (!mm->rm_so) {
            start += skip;
            continue;
          } else mm->rm_eo = mm->rm_so;
        } else {
          if (!rc) break;
          mm->rm_eo = strlen(start);
        }
        mm->rm_so = 0;
      } else if (rc) break;

      // At least one line we didn't print since match while -ABC active
      if (bars) {
        xputs(bars);
        bars = 0;
      }
      matched++;
      TT.found = 1;
      if (toys.optflags & FLAG_q) {
        toys.exitval = 0;
        xexit();
      }
      if (toys.optflags & FLAG_l) {
        xprintf("%s%c", name, TT.outdelim);
        free(line);
        fclose(file);
        return;
      }
      if (toys.optflags & FLAG_o)
        if (mm->rm_eo == mm->rm_so)
          break;

      if (!(toys.optflags & FLAG_c)) {
        long bcount = 1 + offset + (start-line) +
          ((toys.optflags & FLAG_o) ? mm->rm_so : 0);
 
        if (bin) printf("Binary file %s matches\n", name);
        else if (!(toys.optflags & FLAG_o)) {
          while (dlb) {
            struct double_list *dl = dlist_pop(&dlb);
            unsigned *uu = (void *)(dl->data+((strlen(dl->data)+1)|3)+1);

            outline(dl->data, '-', name, lcount-before, uu[0]+1, uu[1]);
            free(dl->data);
            free(dl);
            before--;
          }

          outline(line, ':', name, lcount, bcount, ulen);
          if (TT.A) after = TT.A+1;
        } else outline(start+mm->rm_so, ':', name, lcount, bcount,
                       mm->rm_eo-mm->rm_so);
      }

      start += skip;
      if (!FLAG(o)) break;
    } while (*start);
    offset += len;

    if (matched) mcount++;
    else {
      int discard = (after || TT.B);

      if (after && --after) {
        outline(line, '-', name, lcount, 0, ulen);
        discard = 0;
      }
      if (discard && TT.B) {
        unsigned *uu, ul = (ulen+1)|3;

        line = xrealloc(line, ul+8);
        uu = (void *)(line+ul+1);
        uu[0] = offset-len;
        uu[1] = ulen;
        dlist_add(&dlb, line);
        line = 0;
        if (++before>TT.B) {
          struct double_list *dl;

          dl = dlist_pop(&dlb);
          free(dl->data);
          free(dl);
          before--;
        } else discard = 0;
      }
      // If we discarded a line while displaying context, show bars before next
      // line (but don't show them now in case that was last match in file)
      if (discard && mcount) bars = "--";
    }
    free(line);

    if ((toys.optflags & FLAG_m) && mcount >= TT.m) break;
  }

  if (toys.optflags & FLAG_c) outline(0, ':', name, mcount, 0, -1);

  // loopfiles will also close the fd, but this frees an (opaque) struct.
  fclose(file);
  while (dlb) {
    struct double_list *dl = dlist_pop(&dlb);

    free(dl->data);
    free(dl);
  }
}

static void parse_regex(void)
{
  struct arg_list *al, *new, *list = NULL;
  char *s, *ss;

  // Add all -f lines to -e list. (Yes, this is leaking allocation context for
  // exit to free. Not supporting nofork for this command any time soon.)
  al = TT.f ? TT.f : TT.e;
  while (al) {
    if (TT.f) s = ss = xreadfile(al->arg, 0, 0);
    else s = ss = al->arg;

    // Split lines at \n, add individual lines to new list.
    do {
      ss = strchr(s, '\n');
      if (ss) *(ss++) = 0;
      new = xmalloc(sizeof(struct arg_list));
      new->next = list;
      new->arg = s;
      list = new;
      s = ss;
    } while (ss && *s);

    // Advance, when we run out of -f switch to -e.
    al = al->next;
    if (!al && TT.f) {
      TT.f = 0;
      al = TT.e;
    }
  }
  TT.e = list;

  if (!(toys.optflags & FLAG_F)) {
    int i;

    // Convert regex list
    for (al = TT.e; al; al = al->next) {
      struct reg *shoe = xmalloc(sizeof(struct reg));

      dlist_add_nomalloc(&TT.reg, (void *)shoe);
      i = regcomp(&shoe->r, al->arg,
                  (REG_EXTENDED*!!FLAG(E)) | (REG_ICASE*!!FLAG(i)));
      if (i) {
        regerror(i, &shoe->r, toybuf, sizeof(toybuf));
        error_exit("bad REGEX '%s': %s", al->arg, toybuf);
      }
    }
  }
}

static int do_grep_r(struct dirtree *new)
{
  char *name;

  if (!dirtree_notdotdot(new)) return 0;
  if (S_ISDIR(new->st.st_mode)) return DIRTREE_RECURSE;
  if (TT.S || TT.M) {
    struct arg_list *al;

    for (al = TT.S; al; al = al->next)
      if (!fnmatch(al->arg, new->name, 0)) return 0;

    if (TT.M) {
      for (al = TT.M; al; al = al->next)
        if (!fnmatch(al->arg, new->name, 0)) break;

      if (!al) return 0;
    }
  }

  // "grep -r onefile" doesn't show filenames, but "grep -r onedir" should.
  if (new->parent && !(toys.optflags & FLAG_h)) toys.optflags |= FLAG_H;

  name = dirtree_path(new, 0);
  do_grep(openat(dirtree_parentfd(new), new->name, 0), name);
  free(name);

  return 0;
}

void grep_main(void)
{
  char **ss = toys.optargs;

  // Grep exits with 2 for errors
  toys.exitval = 2;

  if (!TT.A) TT.A = TT.C;
  if (!TT.B) TT.B = TT.C;

  TT.indelim = '\n' * !(toys.optflags&FLAG_z);
  TT.outdelim = '\n' * !(toys.optflags&FLAG_Z);

  // Handle egrep and fgrep
  if (*toys.which->name == 'e') toys.optflags |= FLAG_E;
  if (*toys.which->name == 'f') toys.optflags |= FLAG_F;

  if (!TT.e && !TT.f) {
    if (!*ss) error_exit("no REGEX");
    TT.e = xzalloc(sizeof(struct arg_list));
    TT.e->arg = *(ss++);
    toys.optc--;
  }

  parse_regex();

  if (!(toys.optflags & FLAG_h) && toys.optc>1) toys.optflags |= FLAG_H;

  if (toys.optflags & FLAG_s) {
    close(2);
    xopen_stdio("/dev/null", O_RDWR);
  }

  if (toys.optflags & FLAG_r) {
    // Iterate through -r arguments. Use "." as default if none provided.
    for (ss = *ss ? ss : (char *[]){".", 0}; *ss; ss++) {
      if (!strcmp(*ss, "-")) do_grep(0, *ss);
      else dirtree_read(*ss, do_grep_r);
    }
  } else loopfiles_rw(ss, O_RDONLY|WARN_ONLY, 0, do_grep);
  if (TT.tried == toys.optc || (FLAG(q)&&TT.found)) toys.exitval = !TT.found;
}
