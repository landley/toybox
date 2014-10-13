/* sed.c - stream editor
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/sed.html
 *
 * todo "-e blah -f blah -e blah" what order?
 * What happens when first address matched, then EOF? How about ",42" or "1,"
 * Does $ match last line of file or last line of input
 * If file doesn't end with newline
 * command preceded by whitespace. whitespace before rw or s///w file
 * space before address
 * numerical addresses that cross, select one line
 * test backslash escapes in regex; share code with printf?

USE_SED(NEWTOY(sed, "e*f*inr", TOYFLAG_USR|TOYFLAG_BIN))

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

  void *pattern;
)

static void do_line(char **pline, long len)
{
  printf("len=%ld line=%s\n", len, *pline);
}

static void do_lines(int fd, char *name, void (*call)(char **pline, long len))
{
  FILE *fp = fdopen(fd, "r");

  for (;;) {
    char *line = 0;
    ssize_t len;

    len = getline(&line, (void *)&len, fp);
    do_line(&line, len);
    free(line);
    if (len < 1) break;
  }
}

static void do_sed(int fd, char *name)
{
  do_lines(fd, name, do_line);
}

void sed_main(void)
{
  char **args = toys.optargs;

  // Need a pattern
  if (!TT.e) {
    if (!*toys.optargs) error_exit("no pattern");
    (TT.e = xzalloc(sizeof(struct arg_list)))->arg = *(args++);
  }

  // Inflict pattern upon input files
  loopfiles(args, do_sed);
}
