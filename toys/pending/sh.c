/* sh.c - toybox shell
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * The POSIX-2008/SUSv4 spec for this is at:
 * http://opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
 * and http://opengroup.org/onlinepubs/9699919799/utilities/sh.html
 *
 * The first link describes the following shell builtins:
 *
 *   break colon continue dot eval exec exit export readonly return set shift
 *   times trap unset
 *
 * The second link (the utilities directory) also contains specs for the
 * following shell builtins:
 *
 *   alias bg cd command fc fg getopts hash jobs kill read type ulimit
 *   umask unalias wait
 *
 * Things like the bash man page are good to read too.
 *
 * TODO: // Handle embedded NUL bytes in the command line.

USE_SH(NEWTOY(cd, NULL, TOYFLAG_NOFORK))
USE_SH(NEWTOY(exit, NULL, TOYFLAG_NOFORK))

USE_SH(NEWTOY(sh, "c:i", TOYFLAG_BIN))
USE_SH(OLDTOY(toysh, sh, TOYFLAG_BIN))

config SH
  bool "sh (toysh)"
  default n
  help
    usage: sh [-c command] [script]

    Command shell.  Runs a shell script, or reads input interactively
    and responds to it.

    -c	command line to execute
    -i	interactive mode (default when STDIN is a tty)

config EXIT
  bool
  default n
  depends on SH
  help
    usage: exit [status]

    Exit shell.  If no return value supplied on command line, use value
    of most recent command, or 0 if none.

config CD
  bool
  default n
  depends on SH
  help
    usage: cd [-PL] [path]

    Change current directory.  With no arguments, go $HOME.

    -P	Physical path: resolve symlinks in path.
    -L	Local path: .. trims directories off $PWD (default).
*/

/*
This level of micromanagement is silly, it adds more complexity than it's
worth. (Not just to the code, but decision fatigue configuring it.)

That said, the following list is kept for the moment as a todo list of
features I need to implement.

config SH_PROFILE
  bool "Profile support"
  default n
  depends on SH_TTY
  help
    Read /etc/profile and ~/.profile when running interactively.

    Also enables the built-in command "source".

config SH_JOBCTL
  bool "Job Control (fg, bg, jobs)"
  default n
  depends on SH_TTY
  help
    Add job control to toysh.  This lets toysh handle CTRL-Z, and enables
    the built-in commands "fg", "bg", and "jobs".

    With pipe support, enable use of "&" to run background processes.

config SH_FLOWCTL
  bool "Flow control (if, while, for, functions)"
  default n
  depends on SH
  help
    Add flow control to toysh.  This enables the if/then/else/fi,
    while/do/done, and for/do/done constructs.

    With pipe support, this enables the ability to define functions
    using the "function name" or "name()" syntax, plus curly brackets
    "{ }" to group commands.

config SH_QUOTES
  bool "Smarter argument parsing (quotes)"
  default n
  depends on SH
  help
    Add support for parsing "" and '' style quotes to the toysh command
    parser, with lets arguments have spaces in them.

config SH_WILDCARDS
  bool "Wildcards ( ?*{,} )"
  default n
  depends on SH_QUOTES
  help
    Expand wildcards in argument names, ala "ls -l *.t?z" and
    "rm subdir/{one,two,three}.txt".

config SH_PROCARGS
  bool "Executable arguments ( `` and $() )"
  default n
  depends on SH_QUOTES
  help
    Add support for executing arguments contianing $() and ``, using
    the output of the command as the new argument value(s).

    (Bash calls this "command substitution".)

config SH_ENVVARS
  bool "Environment variable support"
  default n
  depends on SH_QUOTES
  help
    Substitute environment variable values for $VARNAME or ${VARNAME},
    and enable the built-in command "export".

config SH_LOCALS
  bool "Local variables"
  default n
  depends on SH_ENVVARS
  help
    Support for local variables, fancy prompts ($PS1), the "set" command,
    and $?.

config SH_ARRAYS
  bool "Array variables"
  default n
  depends on SH_LOCALS
  help
    Support for ${blah[blah]} style array variables.

config SH_PIPES
  bool "Pipes and redirects ( | > >> < << & && | || () ; )"
  default n
  depends on SH
  help
    Support multiple commands on the same command line.  This includes
    | pipes, > >> < redirects, << here documents, || && conditional
    execution, () subshells, ; sequential execution, and (with job
    control) & background processes.

config SH_BUILTINS
  bool "Builtin commands"
  default n
  depends on SH
  help
    Adds the commands exec, fg, bg, help, jobs, pwd, export, source, set,
    unset, read, alias.
*/

#define FOR_sh
#include "toys.h"

GLOBALS(
  char *command;
)

// A single executable, its arguments, and other information we know about it.
#define SH_FLAG_EXIT    1
#define SH_FLAG_SUSPEND 2
#define SH_FLAG_PIPE    4
#define SH_FLAG_AND     8
#define SH_FLAG_OR      16
#define SH_FLAG_AMP     32
#define SH_FLAG_SEMI    64
#define SH_FLAG_PAREN   128

// What we know about a single process.
struct command {
  struct command *next;
  int flags;              // exit, suspend, && ||
  int pid;                // pid (or exit code)
  int argc;
  char *argv[0];
};

// A collection of processes piped into/waiting on each other.
struct pipeline {
  struct pipeline *next;
  int job_id;
  struct command *cmd;
  char *cmdline;         // Unparsed line for display purposes
  int cmdlinelen;        // How long is cmdline?
};

// Parse one word from the command line, appending one or more argv[] entries
// to struct command.  Handles environment variable substitution and
// substrings.  Returns pointer to next used byte, or NULL if it
// hit an ending token.
static char *parse_word(char *start, struct command **cmd)
{
  char *end;

  // Detect end of line (and truncate line at comment)
  if (strchr("><&|(;", *start)) return 0;

  // Grab next word.  (Add dequote and envvar logic here)
  end = start;
  while (*end && !isspace(*end)) end++;
  (*cmd)->argv[(*cmd)->argc++] = xstrndup(start, end-start);

  // Allocate more space if there's no room for NULL terminator.

  if (!((*cmd)->argc & 7))
    *cmd=xrealloc(*cmd,
        sizeof(struct command) + ((*cmd)->argc+8)*sizeof(char *));
  (*cmd)->argv[(*cmd)->argc] = 0;
  return end;
}

// Parse a line of text into a pipeline.
// Returns a pointer to the next line.

static char *parse_pipeline(char *cmdline, struct pipeline *line)
{
  struct command **cmd = &(line->cmd);
  char *start = line->cmdline = cmdline;

  if (!cmdline) return 0;

  line->cmdline = cmdline;

  // Parse command into argv[]
  for (;;) {
    char *end;

    // Skip leading whitespace and detect end of line.
    while (isspace(*start)) start++;
    if (!*start || *start=='#') {
      line->cmdlinelen = start-cmdline;
      return 0;
    }

    // Allocate next command structure if necessary
    if (!*cmd) *cmd = xzalloc(sizeof(struct command)+8*sizeof(char *));

    // Parse next argument and add the results to argv[]
    end = parse_word(start, cmd);

    // If we hit the end of this command, how did it end?
    if (!end) {
      if (*start) {
        if (*start==';') {
          start++;
          break;
        }
        // handle | & < > >> << || &&
      }
      break;
    }
    start = end;
  }

  line->cmdlinelen = start-cmdline;

  return start;
}

// Execute the commands in a pipeline
static void run_pipeline(struct pipeline *line)
{
  struct toy_list *tl;
  struct command *cmd = line->cmd;
  if (!cmd || !cmd->argc) return;

  tl = toy_find(cmd->argv[0]);
  // Is this command a builtin that should run in this process?
  if (tl && (tl->flags & TOYFLAG_NOFORK)) {
    struct toy_context temp;
    jmp_buf rebound;

    // This fakes lots of what toybox_main() does.
    memcpy(&temp, &toys, sizeof(struct toy_context));
    memset(&toys, 0, sizeof(struct toy_context));

    if (!setjmp(rebound)) {
      toys.rebound = &rebound;
      toy_init(tl, cmd->argv);
      tl->toy_main();
    }
    cmd->pid = toys.exitval;
    if (toys.optargs != toys.argv+1) free(toys.optargs);
    if (toys.old_umask) umask(toys.old_umask);
    memcpy(&toys, &temp, sizeof(struct toy_context));
  } else {
    int status;

    cmd->pid = vfork();
    if (!cmd->pid) xexec(cmd->argv);
    else waitpid(cmd->pid, &status, 0);

    if (WIFEXITED(status)) cmd->pid = WEXITSTATUS(status);
    if (WIFSIGNALED(status)) cmd->pid = WTERMSIG(status);
  }

  return;
}

// Free the contents of a command structure
static void free_cmd(void *data)
{
  struct command *cmd=(struct command *)data;

  while(cmd->argc) free(cmd->argv[--cmd->argc]);
}


// Parse a command line and do what it says to do.
static void handle(char *command)
{
  struct pipeline line;
  char *start = command;

  // Loop through commands in this line

  for (;;) {

    // Parse a group of connected commands

    memset(&line,0,sizeof(struct pipeline));
    start = parse_pipeline(start, &line);
    if (!line.cmd) break;

    // Run those commands

    run_pipeline(&line);
    llist_traverse(line.cmd, free_cmd);
  }
}

void cd_main(void)
{
  char *dest = *toys.optargs ? *toys.optargs : getenv("HOME");

  xchdir(dest ? dest : "/");
}

void exit_main(void)
{
  exit(*toys.optargs ? atoi(*toys.optargs) : 0);
}

void sh_main(void)
{
  FILE *f;

  // Set up signal handlers and grab control of this tty.
  if (isatty(0)) toys.optflags |= FLAG_i;

  f = *toys.optargs ? xfopen(*toys.optargs, "r") : NULL;
  if (TT.command) handle(TT.command);
  else {
    size_t cmdlen = 0;
    for (;;) {
      char *prompt = getenv("PS1"), *command = 0;

      // TODO: parse escapes in prompt
      if (!f) printf("%s", prompt ? prompt : "$ ");
      if (1 > getline(&command, &cmdlen, f ? f : stdin)) break;
      handle(command);
      free(command);
    }
  }

  toys.exitval = 1;
}
