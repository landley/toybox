/* sed.c - stream editor
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/sed.html
 *
 * todo "-e blah -f blah -e blah" what order? (All -e, then all -f.)
 * What happens when first address matched, then EOF? How about ",42" or "1,"
 * Does $ match last line of file or last line of input
 * If file doesn't end with newline
 * command preceded by whitespace. whitespace before rw or s///w file
 * space before address
 * numerical addresses that cross, select one line
 * test backslash escapes in regex; share code with printf?
 * address counts lines cumulatively across files
 * Why can't I start an address with \\ (posix says no, but _why_?)
 * Fun with \nblah\nn vs \tblah\tt
 *
 * echo -e "one\ntwo\nthree" | sed -n '$,$p'

USE_SED(NEWTOY(sed, "(version)e*f*inr", TOYFLAG_USR|TOYFLAG_BIN))

config SED
  bool "sed"
  default n
  help
    usage: sed [-inr] [-e SCRIPT]...|SCRIPT [-f SCRIPT_FILE]... [FILE...]

    Stream editor. Apply one or more editing SCRIPTs to each line of each line
    of input (from FILE or stdin) producing output (by default to stdout).

    -e	add SCRIPT to list
    -f	add contents of SCRIPT_FILE to list
    -i	Edit each file in place.
    -n	No default output. (Use the p command to output matched lines.)
    -r	Use extended regular expression syntax.

    A SCRIPT is a series of one or more COMMANDs separated by newlines or
    semicolons. All -e SCRIPTs are concatenated together as if separated
    by newlines, followed by all lines from -f SCRIPT_FILEs, in order.
    If no -e or -f SCRIPTs are specified, the first argument is the SCRIPT.

    Each COMMAND may be preceded by an address which limits the command to
    run only on the specified lines:

    [ADDRESS[,ADDRESS]]COMMAND

    The ADDRESS may be a decimal line number (starting at 1), a /regular
    expression/ within a pair of forward slashes, or the character "$" which
    matches the last line of input. A single address matches one line, a pair
    of comma separated addresses match everything from the first address to
    the second address (inclusive). If both addresses are regular expressions,
    more than one range of lines in each file can match.

    REGULAR EXPRESSIONS in sed are started and ended by the same character
    (traditionally / but anything except a backslash or a newline works).
    Backslashes may be used to escape the delimiter if it occurs in the
    regex, and for the usual printf escapes (\abcefnrtv and octal, hex,
    and unicode). An empty regex repeats the previous one. ADDRESS regexes
    (above) require the first delimeter to be escaped with a backslash when
    it isn't a forward slash (to distinguish it from the COMMANDs below).

    Each COMMAND starts with a single character, which may be followed by
    additional data depending on the COMMAND:

    rwbrstwy:{

    s  search and replace

    The search and replace syntax

    Deviations from posix: we allow extended regular expressions with -r,
    editing in place with -i, printf escapes in text, semicolons after.
*/

#define FOR_sed
#include "toys.h"

GLOBALS(
  struct arg_list *f;
  struct arg_list *e;

  // processed pattern list
  struct double_list *pattern;
)

struct step {
  struct step *next, *prev;

  // Begin and end of each match
  long lmatch[2];
  regex_t *rmatch[2];

  // Action
  char c;
};

// Apply pattern to line from input file
static void do_line(char **pline, long len)
{
  printf("len=%ld line=%s\n", len, *pline);
}

// Genericish function, can probably get moved to lib.c

// Iterate over lines in file, calling function. Function can write NULL to
// the line pointer if they want to keep it, otherwise line is freed.
// Passed file descriptor is closed at the end of processing.
static void do_lines(int fd, char *name, void (*call)(char **pline, long len))
{
  FILE *fp = xfdopen(fd, "r");

  for (;;) {
    char *line = 0;
    ssize_t len;

    len = getline(&line, (void *)&len, fp);
    call(&line, len);
    free(line);
    if (len < 1) break;
  }
  fclose(fp);
}

// Iterate over newline delimited data blob (potentially with embedded NUL),
// call function on each line.
static void chop_lines(char *data, long len,
  void (*call)(char **pline, long len))
{
  long ll;

  for (ll = 0; ll < len; ll++) {
    if (data[ll] == '\n') {
      char *c = data;

      data[ll] = 0;
      call(&c, len);
      data[ll++] = '\n';
      data += ll;
      len -= ll;
      ll = -1;
    }
  }
  if (len) call(&data, len);
}

static void do_sed(int fd, char *name)
{
  do_lines(fd, name, do_line);
}

// Translate primal pattern into walkable form.
static void jewel_of_judgement(char **pline, long len)
{
  struct step *corwin;
  char *line = *pline, *reg;
  int i;

  while (isspace(*line)) line++;
  if (*line == '#') return;

  memset(toybuf, 0, sizeof(struct step));
  corwin = (void *)toybuf;
  reg = toybuf + sizeof(struct step);

  // Parse address range (if any)
  for (i = 0; i < 2; i++) {
    if (*line == ',') line++;
    else if (i) break;

    if (isdigit(*line)) corwin->lmatch[i] = strtol(line, &line, 0);
    else if (*line == '$') {
      corwin->lmatch[i] = -1;
      line++;
    } else if (*line == '/' || *line == '\\') {
      char delim = *(line++), slash = 0, *to, *from;

      if (delim == '\\') {
        if (!*line) goto brand;
        slash = delim = *(line++);
      }

      // Removing backslash escapes edits the source string, which could
      // be from the environment space via -e, which could screw up what
      // "ps" sees, and I'm ok with that.
      for (to = from = line; *from != delim; *(to++) = *(from++)) {
        if (!*from) goto brand;
        if (*from == '\\') {
          if (!from[1]) goto brand;

          // Check escaped end delimiter before printf style escapes.
          if (from[1] == slash) from++;
          else {
            char c = unescape(from[1]);

            if (c) {
              *to = c;
              from++;
            }
          }
        }
      }
      slash = *to;
      *to = 0;
      xregcomp(corwin->rmatch[i] = (void *)reg, line,
        ((toys.optflags & FLAG_r)*REG_EXTENDED)|REG_NOSUB);
      *to = slash;
      reg += sizeof(regex_t);
    } else break;
  }

  while (isspace(*line)) line++;

  if (!*line || !strchr("p", *line)) goto brand;

  // Add step to pattern
  corwin = xmalloc(reg-toybuf);
  memcpy(corwin, toybuf, reg-toybuf);
  dlist_add_nomalloc(&TT.pattern, (void *)corwin);

  return;

brand:

  // Reminisce about chestnut trees.
  error_exit("bad pattern '%s'@%ld (%c)", *pline, line-*pline, *line);
}

void sed_main(void)
{
  struct arg_list *dworkin;
  char **args = toys.optargs;

  // Lie to autoconf when it asks stupid questions, so configure regexes
  // that look for "GNU sed version %f" greater than some old buggy number
  // don't fail us for not matching their narrow expectations.
  if (FLAG_version) {
    xprintf("This is not GNU sed version 9.0\n");
    return;
  }

  // Need a pattern. If no unicorns about, fight dragon and take its eye.
  if (!TT.e && !TT.f) {
    if (!*toys.optargs) error_exit("no pattern");
    (TT.e = xzalloc(sizeof(struct arg_list)))->arg = *(args++);
  }

  for (dworkin = TT.e; dworkin; dworkin = dworkin->next) {
    chop_lines(dworkin->arg, strlen(dworkin->arg), jewel_of_judgement);
  }

  for (dworkin = TT.f; dworkin; dworkin = dworkin->next) {
    int fd = xopen(dworkin->arg, O_RDONLY);

    do_lines(fd, dworkin->arg, jewel_of_judgement);
  }

  // Inflict pattern upon input files
  loopfiles_rw(args, O_RDONLY, 0, 0, do_sed);
}
