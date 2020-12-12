/* grep.c - show lines matching regular expressions
 *
 * Copyright 2013 CE Strake <strake888 at gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/grep.html
 *
 * Posix doesn't even specify -r, documenting deviations from it is silly.
* echo hello | grep -w ''
* echo '' | grep -w ''
* echo hello | grep -f </dev/null
*

USE_GREP(NEWTOY(grep, "(line-buffered)(color):;(exclude-dir)*S(exclude)*M(include)*ZzEFHIab(byte-offset)h(no-filename)ino(only-matching)rRsvwcl(files-with-matches)q(quiet)(silent)e*f*C#B#A#m#x[!wx][!EFw]", TOYFLAG_BIN|TOYFLAG_ARGFAIL(2)|TOYFLAG_LINEBUF))
USE_EGREP(OLDTOY(egrep, grep, TOYFLAG_BIN|TOYFLAG_ARGFAIL(2)|TOYFLAG_LINEBUF))
USE_FGREP(OLDTOY(fgrep, grep, TOYFLAG_BIN|TOYFLAG_ARGFAIL(2)|TOYFLAG_LINEBUF))

config GREP
  bool "grep"
  default y
  help
    usage: grep [-EFrivwcloqsHbhn] [-ABC NUM] [-m MAX] [-e REGEX]... [-MS PATTERN]... [-f REGFILE] [FILE]...

    Show lines matching regular expressions. If no -e, first argument is
    regular expression to match. With no files (or "-" filename) read stdin.
    Returns 0 if matched, 1 if no match found, 2 for command errors.

    -e  Regex to match. (May be repeated.)
    -f  File listing regular expressions to match.

    file search:
    -r  Recurse into subdirectories (defaults FILE to ".")
    -R  Recurse into subdirectories and symlinks to directories
    -M  Match filename pattern (--include)
    -S  Skip filename pattern (--exclude)
    --exclude-dir=PATTERN  Skip directory pattern
    -I  Ignore binary files

    match type:
    -A  Show NUM lines after     -B  Show NUM lines before match
    -C  NUM lines context (A+B)  -E  extended regex syntax
    -F  fixed (literal match)    -a  always text (not binary)
    -i  case insensitive         -m  match MAX many lines
    -v  invert match             -w  whole word (implies -E)
    -x  whole line               -z  input NUL terminated

    display modes: (default: matched line)
    -c  count of matching lines  -l  show only matching filenames
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

GLOBALS(
  long m, A, B, C;
  struct arg_list *f, *e, *M, *S, *exclude_dir;
  char *color;

  char *purple, *cyan, *red, *green, *grey;
  struct double_list *reg;
  char indelim, outdelim;
  int found, tried;
)

struct reg {
  struct reg *next, *prev;
  int rc;
  regex_t r;
  regmatch_t m;
};

static void numdash(long num, char dash)
{
  printf("%s%ld%s%c", TT.green, num, TT.cyan, dash);
}

// Emit line with various potential prefixes and delimiter
static void outline(char *line, char dash, char *name, long lcount, long bcount,
  unsigned trim)
{
  if (!trim && FLAG(o)) return;
  if (name && FLAG(H)) printf("%s%s%s%c", TT.purple, name, TT.cyan, dash);
  if (FLAG(c)) {
    printf("%s%ld", TT.grey, lcount);
    xputc(TT.outdelim);
  } else if (lcount && FLAG(n)) numdash(lcount, dash);
  if (bcount && FLAG(b)) numdash(bcount-1, dash);
  if (line) {
    if (FLAG(color)) xputsn(FLAG(o) ? TT.red : TT.grey);
    // support embedded NUL bytes in output
    xputsl(line, trim);
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

  if (!FLAG(r)) TT.tried++;
  if (!fd) name = "(standard input)";

  // Only run binary file check on lseekable files.
  if (!FLAG(a) && !lseek(fd, 0, SEEK_CUR)) {
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
    if (bin && FLAG(I)) return;
  }

  if (!(file = fdopen(fd, "r"))) return perror_msg("%s", name);

  // Loop through lines of input
  for (;;) {
    char *line = 0, *start;
    struct reg *shoe;
    size_t ulen;
    long len;
    int matched = 0, rc = 1;

    // get next line, check and trim delimiter
    lcount++;
    errno = 0;
    ulen = len = getdelim(&line, &ulen, TT.indelim, file);
    if (errno) perror_msg("%s", name);
    if (len<1) break;
    if (line[ulen-1] == TT.indelim) line[--ulen] = 0;

    // Prepare for next line
    start = line;
    if (TT.reg) for (shoe = (void *)TT.reg; shoe; shoe = shoe->next)
      shoe->rc = 0;

    // Loop to handle multiple matches in same line
    do {
      regmatch_t *mm = (void *)toybuf;

      // Handle "fixed" (literal) matches
      if (FLAG(F)) {
        struct arg_list *seek, fseek;
        char *s = 0;

        for (seek = TT.e; seek; seek = seek->next) {
          if (FLAG(x)) {
            if (!(FLAG(i) ? strcasecmp : strcmp)(seek->arg, line)) s = line;
          } else if (!*seek->arg) {
            // No need to set fseek.next because this will match every line.
            seek = &fseek;
            fseek.arg = s = line;
          } else if (FLAG(i)) s = strcasestr(start, seek->arg);
          else s = strstr(start, seek->arg);

          if (s) break;
        }

        if (s) {
          rc = 0;
          mm->rm_so = (s-start);
          mm->rm_eo = (s-start)+strlen(seek->arg);
        } else rc = 1;

      // Handle regex matches
      } else {
        int baseline = mm->rm_eo;

        mm->rm_so = mm->rm_eo = INT_MAX;
        rc = 1;
        for (shoe = (void *)TT.reg; shoe; shoe = shoe->next) {

          // Do we need to re-check this regex?
          if (!shoe->rc) {
            shoe->m.rm_so -= baseline;
            shoe->m.rm_eo -= baseline;
            if (!matched || shoe->m.rm_so<0)
              shoe->rc = regexec0(&shoe->r, start, ulen-(start-line), 1,
                                  &shoe->m, start==line ? 0 : REG_NOTBOL);
          }

          // If we got a match, is it a _better_ match?
          if (!shoe->rc && (shoe->m.rm_so < mm->rm_so ||
              (shoe->m.rm_so == mm->rm_so && shoe->m.rm_eo >= mm->rm_eo)))
          {
            mm = &shoe->m;
            rc = 0;
          }
        }
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

      if (FLAG(v)) {
        if (FLAG(o)) {
          if (rc) {
            mm->rm_so = 0;
            mm->rm_eo = ulen-(start-line);
          } else if (!mm->rm_so) {
            start += mm->rm_eo;
            continue;
          } else mm->rm_eo = mm->rm_so;
        } else {
          if (!rc) break;
          mm->rm_eo = ulen-(start-line);
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
      if (FLAG(q)) {
        toys.exitval = 0;
        xexit();
      }
      if (FLAG(l)) {
        xprintf("%s%c", name, TT.outdelim);
        free(line);
        fclose(file);
        return;
      }

      if (!FLAG(c)) {
        long bcount = 1 + offset + (start-line) + (FLAG(o) ? mm->rm_so : 0);

        if (bin) printf("Binary file %s matches\n", name);
        else if (FLAG(o))
          outline(start+mm->rm_so, ':', name, lcount, bcount,
                  mm->rm_eo-mm->rm_so);
        else {
          while (dlb) {
            struct double_list *dl = dlist_pop(&dlb);
            unsigned *uu = (void *)(dl->data+(strlen(dl->data)|3)+1);

            outline(dl->data, '-', name, lcount-before, uu[0]+1, uu[1]);
            free(dl->data);
            free(dl);
            before--;
          }

          if (matched==1)
            outline(FLAG(color) ? 0 : line, ':', name, lcount, bcount, ulen);
          if (FLAG(color)) {
            xputsn(TT.grey);
            if (mm->rm_so) xputsl(line, mm->rm_so);
            xputsn(TT.red);
            xputsl(line+mm->rm_so, mm->rm_eo-mm->rm_so);
          }

          if (TT.A) after = TT.A+1;
        }
      }

      start += mm->rm_eo;
      if (mm->rm_so == mm->rm_eo) break;
      if (!FLAG(o) && FLAG(color)) break;
    } while (*start);
    offset += len;

    if (matched) {
      // Finish off pending line color fragment.
      if (FLAG(color) && !FLAG(o)) {
        xputsn(TT.grey);
        if (ulen > start-line) xputsl(start, ulen-(start-line));
        xputc(TT.outdelim);
      }
      mcount++;
    } else {
      int discard = (after || TT.B);

      if (after && --after) {
        outline(line, '-', name, lcount, 0, ulen);
        discard = 0;
      }
      if (discard && TT.B) {
        unsigned *uu, ul = (ulen|3)+1;

        line = xrealloc(line, ul+8);
        uu = (void *)(line+ul);
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

    if (FLAG(m) && mcount >= TT.m) break;
  }

  if (FLAG(c)) outline(0, ':', name, mcount, 0, 1);

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
// TODO: NUL terminated input shouldn't split -e at \n
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

  if (!FLAG(F)) {
    // Convert regex list
    for (al = TT.e; al; al = al->next) {
      struct reg *shoe;

      if (FLAG(o) && !*al->arg) continue;
      dlist_add_nomalloc(&TT.reg, (void *)(shoe = xmalloc(sizeof(struct reg))));
      xregcomp(&shoe->r, al->arg,
               (REG_EXTENDED*!!FLAG(E))|(REG_ICASE*!!FLAG(i)));
    }
    dlist_terminate(TT.reg);
  }
}

static int do_grep_r(struct dirtree *new)
{
  struct arg_list *al;
  char *name;

  if (!new->parent) TT.tried++;
  if (!dirtree_notdotdot(new)) return 0;
  if (S_ISDIR(new->st.st_mode)) {
    for (al = TT.exclude_dir; al; al = al->next)
      if (!fnmatch(al->arg, new->name, 0)) return 0;
    return DIRTREE_RECURSE|(FLAG(R)?DIRTREE_SYMFOLLOW:0);
  }
  if (TT.S || TT.M) {
    for (al = TT.S; al; al = al->next)
      if (!fnmatch(al->arg, new->name, 0)) return 0;

    if (TT.M) {
      for (al = TT.M; al; al = al->next)
        if (!fnmatch(al->arg, new->name, 0)) break;

      if (!al) return 0;
    }
  }

  // "grep -r onefile" doesn't show filenames, but "grep -r onedir" should.
  if (new->parent && !FLAG(h)) toys.optflags |= FLAG_H;

  name = dirtree_path(new, 0);
  do_grep(openat(dirtree_parentfd(new), new->name, 0), name);
  free(name);

  return 0;
}

void grep_main(void)
{
  char **ss = toys.optargs;

  if (FLAG(color) && (!TT.color || !strcmp(TT.color, "auto")) && !isatty(1))
    toys.optflags &= ~FLAG_color;

  if (FLAG(color)) {
    TT.purple = "\033[35m";
    TT.cyan = "\033[36m";
    TT.red = "\033[1;31m";
    TT.green = "\033[32m";
    TT.grey = "\033[0m";
  } else TT.purple = TT.cyan = TT.red = TT.green = TT.grey = "";

  if (FLAG(R)) toys.optflags |= FLAG_r;

  // Grep exits with 2 for errors
  toys.exitval = 2;

  if (!TT.A) TT.A = TT.C;
  if (!TT.B) TT.B = TT.C;

  TT.indelim = '\n' * !FLAG(z);
  TT.outdelim = '\n' * !FLAG(Z);

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

  if (!FLAG(h) && toys.optc>1) toys.optflags |= FLAG_H;

  if (FLAG(s)) {
    close(2);
    xopen_stdio("/dev/null", O_RDWR);
  }

  if (FLAG(r)) {
    // Iterate through -r arguments. Use "." as default if none provided.
    for (ss = *ss ? ss : (char *[]){".", 0}; *ss; ss++) {
      if (!strcmp(*ss, "-")) do_grep(0, *ss);
      else dirtree_read(*ss, do_grep_r);
    }
  } else loopfiles_rw(ss, O_RDONLY|WARN_ONLY, 0, do_grep);
  if (TT.tried >= toys.optc || (FLAG(q)&&TT.found)) toys.exitval = !TT.found;
}
