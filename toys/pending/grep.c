/* grep.c - print lines what match given regular expression
 *
 * Copyright 2013 CE Strake <strake888 at gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/grep.html

USE_GREP(NEWTOY(grep, "EFHabhinosvwclqe*f*m#x[!wx][!EFw]", TOYFLAG_BIN))
USE_GREP(OLDTOY(egrep, grep, OPTSTR_grep, TOYFLAG_BIN))
USE_GREP(OLDTOY(fgrep, grep, OPTSTR_grep, TOYFLAG_BIN))

config GREP
  bool "grep"
  default n
  help
    usage: grep [-EFivwcloqsHbhn] [-m MAX] [-e REGEX]... [-f REGFILE] [FILE]...

    Show lines matching regular expressions. If no -e, first argument is
    regular expression to match. With no files (or "-" filename) read stdin.
    Returns 0 if matched, 1 if no match found.

    -e  Regex to match. (May be repeated.)
    -f  File containing regular expressions to match.

    match type:
    -E  extended regex syntax    -F  fixed (match literal string)
    -i  case insensitive         -v  invert match
    -w  whole word (implies -E)  -m  stop after this many lines matched
    -x  whole line

    display modes: (default: matched line)
    -c  count of matching lines  -l  show matching filenames
    -o  only matching part       -q  quiet (errors only)
    -s  silent (no error msg)    

    prefix modes (default: filename if checking more than 1 file)
    -H  force filename           -b  byte offset of match
    -h  hide filename            -n  line number of match
*/

#define FOR_grep
#include "toys.h"
#include <regex.h>

GLOBALS(
  long m;
  struct arg_list *f;
  struct arg_list *e;

  char *regstr;
)

static void do_grep(int fd, char *name)
{
  FILE *file = fdopen(fd, "r");
  long offset = 0;
  int lcount = 0, mcount = 0, which = toys.optflags & FLAG_w ? 2 : 0;

  if (!fd) name = "(standard input)";

  if (!file) {
    perror_msg("%s", name);
    return;
  }

  for (;;) {
    char *line = 0, *start;
    regmatch_t matches[3];
    size_t unused;
    long len;
    int mmatch = 0;

    lcount++;
    if (0 > (len = getline(&line, &unused, file))) break;
    if (line[len-1] == '\n') line[len-1] = 0;

    start = line;

    for (;;)
    {
      int rc = 0, skip = 0;

      if (toys.optflags & FLAG_F) {
        struct arg_list *seek;
        char *s = 0;

        for (seek = TT.e; seek; seek = seek->next) {

          if (toys.optflags & FLAG_i) {
            long ll = strlen(seek->arg);;

            // Alas, posix hasn't got strcasestr()
            for (s = line; *s; s++) if (!strncasecmp(s, seek->arg, ll)) break;
            if (!*s) s = 0;
          } else s = strstr(line, seek->arg);
          if (s) break;
        }

        if (s) {
          matches[which].rm_so = (s-line);
          skip = matches[which].rm_eo = (s-line)+strlen(seek->arg);
        } else rc = 1;
      } else {
        rc = regexec((regex_t *)toybuf, start, 3, matches,
                     start==line ? 0 : REG_NOTBOL);
        skip = matches[which].rm_eo;
      }

      if (toys.optflags & FLAG_x)
        if (matches[which].rm_so || line[matches[which].rm_eo]) rc = 1;

      if (toys.optflags & FLAG_v) {
        if (toys.optflags & FLAG_o) {
          if (rc) skip = matches[which].rm_eo = strlen(start);
          else if (!matches[which].rm_so) {
            start += skip;
            continue;
          } else matches[which].rm_eo = matches[which].rm_so;
        } else {
          if (!rc) break;
          matches[which].rm_eo = strlen(start);
        }
        matches[which].rm_so = 0;
      } else if (rc) break;

      mmatch++;
      if (toys.optflags & FLAG_q) {
        toys.exitval = 0;
        xexit();
      }
      if (toys.optflags & FLAG_l) {
        printf("%s\n", name);
        free(line);
        fclose(file);
        return;
      }
      if (!(toys.optflags & FLAG_c)) {
        if (!(toys.optflags & FLAG_h)) printf("%s:", name);
        if (toys.optflags & FLAG_n) printf("%d:", lcount);
        if (toys.optflags & FLAG_b)
          printf("%ld:", offset + (start-line) +
              ((toys.optflags & FLAG_o) ? matches[which].rm_so : 0));
        if (!(toys.optflags & FLAG_o)) xputs(line);
        else {
          xprintf("%.*s\n", matches[which].rm_eo - matches[which].rm_so,
                  start + matches[which].rm_so);
        }
      }

      start += skip;
      if (!(toys.optflags & FLAG_o) || !*start) break;
    }
    offset += len;

    free(line);

    if (mmatch) mcount++;
    if ((toys.optflags & FLAG_m) && mcount >= TT.m) break;
  }

  if (toys.optflags & FLAG_c) {
    if (!(toys.optflags & FLAG_h)) printf("%s:", name);
    xprintf("%d\n", mcount);
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
    if (TT.f) s = ss = xreadfile(al->arg);
    else s = ss = al->arg;

    while (ss && *s) {
      ss = strchr(s, '\n');
      if (ss) *(ss++) = 0;
      new = xmalloc(sizeof(struct arg_list));
      new->next = list;
      new->arg = s;
      list = new;
      s = ss;
    }
    al = al->next;
    if (!al && TT.f) {
      TT.f = 0;
      al = TT.e;
    }
  }
  TT.e = list;

  if (!(toys.optflags & FLAG_F)) {
    int w = toys.optflags & FLAG_w;

    // Convert strings to one big regex
    if (w) len = 36;
    for (al = TT.e; al; al = al->next) len += strlen(al->arg)+1;

    TT.regstr = s = xmalloc(len);
    if (w) s = stpcpy(s, "(^|[^_[:alnum:]])(");
    for (al = TT.e; al; al = al->next) {
      s = stpcpy(s, al->arg);
      if (!(toys.optflags & FLAG_E)) *(s++) = '\\';
      *(s++) = '|';
    }
    *(s-=(1+!(toys.optflags & FLAG_E))) = 0;
    if (w) strcpy(s, ")($|[^_[:alnum:]])");

    w = regcomp((regex_t *)toybuf, TT.regstr,
                ((toys.optflags & FLAG_E) ? REG_EXTENDED : 0) |
                ((toys.optflags & FLAG_i) ? REG_ICASE    : 0));

    if (w) {
      regerror(w, (regex_t *)toybuf, toybuf+sizeof(regex_t),
               sizeof(toybuf)-sizeof(regex_t));
      error_exit("bad REGEX: %s", toybuf);
    }
  }
}

void grep_main(void)
{
  // Handle egrep and fgrep
  if (*toys.which->name == 'e' || (toys.optflags & FLAG_w))
    toys.optflags |= FLAG_E;
  if (*toys.which->name == 'f') toys.optflags |= FLAG_F;

  if (!TT.e && !TT.f) {
    if (!*toys.optargs) error_exit("no REGEX");
    TT.e = xzalloc(sizeof(struct arg_list));
    TT.e->arg = *(toys.optargs++);
    toys.optc--;
  }

  parse_regex();

  if (!(toys.optflags & FLAG_H) && (toys.optc < 2)) toys.optflags |= FLAG_h;

  toys.exitval = 1;
  if (toys.optflags & FLAG_s) {
    close(2);
    xopen("/dev/null", O_RDWR);
  }
  loopfiles_rw(toys.optargs, O_RDONLY, 0, 1, do_grep);
  xexit();
}
