/* grep.c - print lines what match given regular expression
 *
 * Copyright 2013 CE Strake <strake888 at gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/grep.html
 *
 * TODO: -ABC

USE_GREP(NEWTOY(grep, "ZzEFHabhinorsvwclqe*f*m#x[!wx][!EFw]", TOYFLAG_BIN))
USE_EGREP(OLDTOY(egrep, grep, TOYFLAG_BIN))
USE_FGREP(OLDTOY(fgrep, grep, TOYFLAG_BIN))

config GREP
  bool "grep"
  default y
  help
    usage: grep [-EFivwcloqsHbhn] [-m MAX] [-e REGEX]... [-f REGFILE] [FILE]...

    Show lines matching regular expressions. If no -e, first argument is
    regular expression to match. With no files (or "-" filename) read stdin.
    Returns 0 if matched, 1 if no match found.

    -e  Regex to match. (May be repeated.)
    -f  File containing regular expressions to match.

    match type:
    -E  extended regex syntax    -F  fixed (match literal string)
    -i  case insensitive         -m  stop after this many lines matched
    -r  recursive (on dir)       -v  invert match
    -w  whole word (implies -E)  -x  whole line
    -z  input NUL terminated

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
  long m;
  struct arg_list *f;
  struct arg_list *e;
)

// Show matches in one file
static void do_grep(int fd, char *name)
{
  FILE *file = fdopen(fd, "r");
  long offset = 0;
  int lcount = 0, mcount = 0;
  char indelim = '\n' * !(toys.optflags&FLAG_z),
       outdelim = '\n' * !(toys.optflags&FLAG_Z);

  if (!fd) name = "(standard input)";

  if (!file) {
    perror_msg("%s", name);
    return;
  }

  // Loop through lines of input
  for (;;) {
    char *line = 0, *start;
    regmatch_t matches;
    size_t unused;
    long len;
    int mmatch = 0;

    lcount++;
    if (0 > (len = getdelim(&line, &unused, indelim, file))) break;
    if (line[len-1] == indelim) line[len-1] = 0;

    start = line;

    // Loop through matches in this line
    do {
      int rc = 0, skip = 0;

      // Handle non-regex matches
      if (toys.optflags & FLAG_F) {
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
          if (toys.optflags & FLAG_i) {
            long ll = strlen(seek->arg);;

            // Alas, posix hasn't got strcasestr()
            for (s = line; *s; s++) if (!strncasecmp(s, seek->arg, ll)) break;
            if (!*s) s = 0;
          } else s = strstr(line, seek->arg);
          if (s) break;
        }

        if (s) {
          matches.rm_so = (s-line);
          skip = matches.rm_eo = (s-line)+strlen(seek->arg);
        } else rc = 1;
      } else {
        rc = regexec((regex_t *)toybuf, start, 1, &matches,
                     start==line ? 0 : REG_NOTBOL);
        skip = matches.rm_eo;
      }

      if (toys.optflags & FLAG_x)
        if (matches.rm_so || line[matches.rm_eo]) rc = 1;

      if (!rc && (toys.optflags & FLAG_w)) {
        char c = 0;

        if ((start+matches.rm_so)!=line) {
          c = start[matches.rm_so-1];
          if (!isalnum(c) && c != '_') c = 0;
        }
        if (!c) {
          c = start[matches.rm_eo];
          if (!isalnum(c) && c != '_') c = 0;
        }
        if (c) {
          start += matches.rm_so+1;

          continue;
        }
      }

      if (toys.optflags & FLAG_v) {
        if (toys.optflags & FLAG_o) {
          if (rc) skip = matches.rm_eo = strlen(start);
          else if (!matches.rm_so) {
            start += skip;
            continue;
          } else matches.rm_eo = matches.rm_so;
        } else {
          if (!rc) break;
          matches.rm_eo = strlen(start);
        }
        matches.rm_so = 0;
      } else if (rc) break;

      mmatch++;
      toys.exitval = 0;
      if (toys.optflags & FLAG_q) xexit();
      if (toys.optflags & FLAG_l) {
        printf("%s%c", name, outdelim);
        free(line);
        fclose(file);
        return;
      }
      if (toys.optflags & FLAG_o)
        if (matches.rm_eo == matches.rm_so)
          break;

      if (!(toys.optflags & FLAG_c)) {
        if (toys.optflags & FLAG_H) printf("%s:", name);
        if (toys.optflags & FLAG_n) printf("%d:", lcount);
        if (toys.optflags & FLAG_b)
          printf("%ld:", offset + (start-line) +
              ((toys.optflags & FLAG_o) ? matches.rm_so : 0));
        if (!(toys.optflags & FLAG_o)) xprintf("%s%c", line, outdelim);
        else {
          xprintf("%.*s%c", matches.rm_eo - matches.rm_so,
                  start + matches.rm_so, outdelim);
        }
      }

      start += skip;
      if (!(toys.optflags & FLAG_o)) break;
    } while (*start);
    offset += len;

    free(line);

    if (mmatch) mcount++;
    if ((toys.optflags & FLAG_m) && mcount >= TT.m) break;
  }

  if (toys.optflags & FLAG_c) {
    if (toys.optflags & FLAG_H) printf("%s:", name);
    xprintf("%d%c", mcount, outdelim);
  }

  // loopfiles will also close the fd, but this frees an (opaque) struct.
  fclose(file);
}

static void parse_regex(void)
{
  struct arg_list *al, *new, *list = NULL;
  long len = 0;
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
    char *regstr;
    int i;

    // Convert strings to one big regex
    for (al = TT.e; al; al = al->next)
      len += strlen(al->arg)+1+!(toys.optflags & FLAG_E);

    regstr = s = xmalloc(len);
    for (al = TT.e; al; al = al->next) {
      s = stpcpy(s, al->arg);
      if (!(toys.optflags & FLAG_E)) *(s++) = '\\';
      *(s++) = '|';
    }
    *(s-=(1+!(toys.optflags & FLAG_E))) = 0;

    i = regcomp((regex_t *)toybuf, regstr,
                ((toys.optflags & FLAG_E) ? REG_EXTENDED : 0) |
                ((toys.optflags & FLAG_i) ? REG_ICASE    : 0));

    if (i) {
      regerror(i, (regex_t *)toybuf, toybuf+sizeof(regex_t),
               sizeof(toybuf)-sizeof(regex_t));
      error_exit("bad REGEX: %s", toybuf);
    }
  }
}

static int do_grep_r(struct dirtree *new)
{
  char *name;

  if (new->parent && !dirtree_notdotdot(new)) return 0;
  if (S_ISDIR(new->st.st_mode)) return DIRTREE_RECURSE;

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

  toys.exitval = 1;
  if (toys.optflags & FLAG_s) {
    close(2);
    xopen("/dev/null", O_RDWR);
  }

  if (toys.optflags & FLAG_r) {
    // Iterate through -r arguments. Use "." as default if none provided.
    for (ss = *ss ? ss : (char *[]){".", 0}; *ss; ss++) {
      if (!strcmp(*ss, "-")) do_grep(0, *ss);
      else dirtree_read(*ss, do_grep_r);
    }
  } else loopfiles_rw(ss, O_RDONLY, 0, 1, do_grep);
}
