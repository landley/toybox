/* sed.c - Stream editor.
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/sed.c

USE_SED(NEWTOY(sed, "irne*", TOYFLAG_BIN))

config SED
  bool "sed"
  default n
  help
    usage: sed [-irn] {command | [-e command]...} [FILE...]

    Stream EDitor, transforms text by appling script of command to each line
    of input.

    -e  Add expression to the command script (if no -e, use first argument)
    -i	Modify file in place
    -n  No default output (p commands only)
    -r  Use extended regular expression syntex
*/

#define FOR_sed
#include "toys.h"
#include "lib/xregcomp.h"

GLOBALS(
  struct arg_list *scripts;
  struct double_list *commands;

  void *parsed;
)

// Digested version of what sed commands can actually tell use to do.


struct sed_command {
  // double_list compatibility (easier to create in-order)
  struct sed_command *next, *prev;

  // data string for (saicytb)
  char c, *data;
  // Regexes for s/match/data/ and /begin/,/end/command
  regex_t *match, *begin, *end;
  // For numeric ranges ala 10,20command
  long lstart, lstop;
  // Which match to replace, 0 for all. s and w commands can write to a file
  int which, outfd;
};

//  Space. Space. Gotta get past space. Spaaaaaaaace! (But not newline.)
void spaceorb(char **s)
{
  while (**s == ' ' || **s == '\t') *s++;
}

void parse_scripts(void)
{
  struct sed_command *commands = 0;
  struct arg_list *script;
  int which = 0;
  long l;

  for (script = TT.scripts; *script; script = script->next) {
    char *str = script->arg, *s;
    struct sed_command *cmd;

    which++;
    for (i=1;;) {
      if (!*str) break;

      cmd = xzalloc(sizeof(struct sed_command));

      // Identify prefix
      for (;;) {
        long l;

        spaceorb(&str);
        if (*str == '$') {
          l = -1;
          str++;
        } else if (isdigit(*str)) l = strtol(str, &str, 10);
        else if (!cmd->lstart) break;
        else goto parse_fail;

        spaceorb(&str);
        if (!cmd->lstart) {
          if (!l) goto parse_fail;
          cmd->lstart = l;
          if (*str != ',') break;
          str++;
          continue;
        }
        cmd->lstop = l;
        break;
      } else if (*str == '/') {
        printf("regex\n");
      }
      l = stridx("{bcdDgGhHlnNpPstwxyrqia= \t#:}", *str);
      if (l == -1) goto parse_fail;


    }
  }

  return;

parse_fail:
  error_exit("bad expression %d@%d: %s", which, i, script->arg+i);
}

void sed_main(void)
{
  char **files=toys.optargs;

  // If no -e, use first argument
  if (!TT.scripts) {
    if (!*files) error_exit("Need script");
    (TT.scripts=xzalloc(sizeof(struct arg_list)))->arg=*(files++);
  }


  {
    struct arg_list *test;

    for (test = TT.commands; test; test = test->next)
      dprintf(2,"command=%s\n",test->arg);
    while (*files) dprintf(2,"file=%s\n", *(files++));
  }
}
