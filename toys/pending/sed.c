/* sed.c - Stream editor.
 *
 * Copyright 2012 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/sed.c

USE_SED(NEWTOY(sed, "irne*f*", TOYFLAG_BIN))

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
  struct arg_list *files;
  struct arg_list *scripts;

  void *commands;
)

// Digested version of what sed commands can actually tell use to do.


struct sed_command {
  // double_list compatibility (easier to create in-order)
  struct sed_command *next, *prev;

  // data string for (saicytb)
  char c, *data;
  // Regexes for s/match/data/ and /begin/,/end/command
  regex_t *rmatch, *rbegin, *rend;
  // For numeric ranges ala 10,20command
  long lstart, lstop;
  // Which match to replace, 0 for all. s and w commands can write to a file
  int which, outfd;
};

//  Space. Space. Gotta get past space. Spaaaaaaaace! (But not newline.)
static void spaceorb(char **s)
{
  while (**s == ' ' || **s == '\t') ++*s;
}

// Parse sed commands

static void parse_scripts(void)
{
  struct arg_list *script;
  int which = 0, i;

  // Loop through list of scripts collated from command line and/or files

  for (script = TT.scripts; script; script = script->next) {
    char *str = script->arg;
    struct sed_command *cmd;

    // we can get multiple commands from a string (semicolons and such)

    which++;
    for (i=1;;) {
      if (!*str) break;

      cmd = xzalloc(sizeof(struct sed_command));

      // Identify prefix
      for (;;) {
        spaceorb(&str);
        if (*str == '^') {
          if (cmd->lstart) goto parse_fail;
          cmd->lstart = -1;
          str++;
          continue;
        } else if (*str == '$') {
          cmd->lstop = LONG_MAX;
          str++;
          break;
        } else if (isdigit(*str)) {
          long ll = strtol(str, &str, 10);

          if (ll<0) goto parse_fail;
          if (cmd->lstart) {
            cmd->lstop = ll;
            break;
          } else cmd->lstart = ll;
        } else if (*str == '/' || *str == '\\') {
          // set begin/end
          printf("regex\n");
          exit(1);
        } else if (!cmd->lstart && !cmd->rbegin) break;
        else goto parse_fail;  // , with no range after it

        spaceorb(&str);
        if (*str != ',') break;
        str++;
      }
      i = stridx("{bcdDgGhHlnNpPstwxyrqia= \t#:}", *str);
      if (i == -1) goto parse_fail;

      dlist_add_nomalloc((struct double_list **)&TT.commands,
                         (struct double_list *)cmd);
      exit(1);
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
    (TT.scripts = xzalloc(sizeof(struct arg_list)))->arg = *(files++);
  }

  parse_scripts();

  while (*files) dprintf(2,"file=%s\n", *(files++));
}
