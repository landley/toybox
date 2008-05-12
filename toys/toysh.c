/* vi: set sw=4 ts=4:
 *
 * toysh - toybox shell
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * The spec for this is at:
 * http://www.opengroup.org/onlinepubs/009695399/utilities/xcu_chap02.html
 * and http://www.opengroup.org/onlinepubs/009695399/utilities/sh.html
 *
 * There are also specs for:
 * http://www.opengroup.org/onlinepubs/009695399/utilities/cd.html
 * http://www.opengroup.org/onlinepubs/009695399/utilities/exit.html
 *
 * Things like the bash man page are good to read too.
 *
 * TODO: // Handle embedded NUL bytes in the command line.

USE_TOYSH(NEWTOY(cd, NULL, TOYFLAG_NOFORK))
USE_TOYSH(NEWTOY(exit, NULL, TOYFLAG_NOFORK))
USE_TOYSH(OLDTOY(sh, toysh, "c:i", TOYFLAG_BIN))
USE_TOYSH(NEWTOY(toysh, "c:i", TOYFLAG_BIN))

config TOYSH
	bool "sh (toysh)"
	default y
	help
	  usage: sh [-c command] [script]

	  The toybox command shell.  Runs a shell script, or else reads input
	  interactively and responds to it.

	  -c	command line to execute

config TOYSH_TTY
	bool "Interactive shell (terminal control)"
	default n
	depends on TOYSH
	help
	  Add terminal control to toysh.  This is necessary for interactive use,
	  so the shell isn't killed by CTRL-C.

config TOYSH_PROFILE
	bool "Profile support"
	default n
	depends on TOYSH_TTY
	help
	  Read /etc/profile and ~/.profile when running interactively.

	  Also enables the built-in command "source".

config TOYSH_JOBCTL
	bool "Job Control (fg, bg, jobs)"
	default n
	depends on TOYSH_TTY
	help
	  Add job control to toysh.  This lets toysh handle CTRL-Z, and enables
	  the built-in commands "fg", "bg", and "jobs".

	  With pipe support, enable use of "&" to run background processes.

config TOYSH_FLOWCTL
	bool "Flow control (if, while, for, functions)"
	default n
	depends on TOYSH
	help
	  Add flow control to toysh.  This enables the if/then/else/fi,
	  while/do/done, and for/do/done constructs.

	  With pipe support, this enables the ability to define functions
	  using the "function name" or "name()" syntax, plus curly brackets
	  "{ }" to group commands.

config TOYSH_QUOTES
	bool "Smarter argument parsing (quotes)"
	default n
	depends on TOYSH
	help
	  Add support for parsing "" and '' style quotes to the toysh command
	  parser, with lets arguments have spaces in them.

config TOYSH_WILDCARDS
	bool "Wildcards ( ?*{,} )"
	default n
	depends on TOYSH_QUOTES
	help
	  Expand wildcards in argument names, ala "ls -l *.t?z" and
	  "rm subdir/{one,two,three}.txt".

config TOYSH_PROCARGS
	bool "Executable arguments ( `` and $() )"
	default n
	depends on TOYSH_QUOTES
	help
	  Add support for executing arguments contianing $() and ``, using
	  the output of the command as the new argument value(s).

	  (Bash calls this "command substitution".)

config TOYSH_ENVVARS
	bool "Environment variable support"
	default n
	depends on TOYSH_QUOTES
	help
	  Substitute environment variable values for $VARNAME or ${VARNAME},
	  and enable the built-in command "export".

config TOYSH_LOCALS
	bool "Local variables"
	default n
	depends on TOYSH_ENVVARS
	help
	  Support for local variables, fancy prompts ($PS1), the "set" command,
	  and $?.

config TOYSH_ARRAYS
	bool "Array variables"
	default n
	depends on TOYSH_LOCALS
	help
	  Support for ${blah[blah]} style array variables.

config TOYSH_PIPES
	bool "Pipes and redirects ( | > >> < << & && | || () ; )"
	default n
	depends on TOYSH
	help
	  Support multiple commands on the same command line.  This includes
	  | pipes, > >> < redirects, << here documents, || && conditional
	  execution, () subshells, ; sequential execution, and (with job
	  control) & background processes.

config TOYSH_BUILTINS
	bool "Builtin commands"
	default n
	depends on TOYSH
	help
	  Adds the commands exec, fg, bg, help, jobs, pwd, export, source, set,
	  unset, read, alias.

config EXIT
	bool
	default n
	depends on TOYSH
	help
	  usage: exit [status]

	  Exit shell.  If no return value supplied on command line, use value
	  of most recent command, or 0 if none.

config CD
	bool
	default n
	depends on TOYSH
	help
	  usage: cd [path]

	  Change current directory.  With no arguments, go to $HOME.

config CD_P
	bool # "-P support for cd"
	default n
	depends on TOYSH
	help
	  usage: cd [-PL]

	  -P    Physical path: resolve symlinks in path.
	  -L    Cancel previous -P and restore default behavior.
*/

#include "toys.h"

DEFINE_GLOBALS(
	char *command;
)

#define TT this.toysh

// A single executable, its arguments, and other information we know about it.
#define TOYSH_FLAG_EXIT    1
#define TOYSH_FLAG_SUSPEND 2
#define TOYSH_FLAG_PIPE    4
#define TOYSH_FLAG_AND     8
#define TOYSH_FLAG_OR      16
#define TOYSH_FLAG_AMP     32
#define TOYSH_FLAG_SEMI    64
#define TOYSH_FLAG_PAREN   128

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
	if (CFG_TOYSH_PIPES && strchr("><&|(;", *start)) return 0;

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

	if (CFG_TOYSH_JOBCTL) line->cmdline = cmdline;

	// Parse command into argv[]
	for (;;) {
		char *end;

		// Skip leading whitespace and detect end of line.
		while (isspace(*start)) start++;
		if (!*start || *start=='#') {
			if (CFG_TOYSH_JOBCTL) line->cmdlinelen = start-cmdline;
			return 0;
		}

		// Allocate next command structure if necessary
		if (!*cmd) *cmd = xzalloc(sizeof(struct command)+8*sizeof(char *));

		// Parse next argument and add the results to argv[]
		end = parse_word(start, cmd);

		// If we hit the end of this command, how did it end?
		if (!end) {
			if (CFG_TOYSH_PIPES && *start) {
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

	if (CFG_TOYSH_JOBCTL) line->cmdlinelen = start-cmdline;

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

		// This fakes lots of what toybox_main() does.
		memcpy(&temp, &toys, sizeof(struct toy_context));
		bzero(&toys, sizeof(struct toy_context));
		toy_init(tl, cmd->argv);
		tl->toy_main();
		cmd->pid = toys.exitval;
		free(toys.optargs);
		if (toys.old_umask) umask(toys.old_umask);
		memcpy(&toys, &temp, sizeof(struct toy_context));
	} else {
		int status;

		cmd->pid = vfork();
		if (!cmd->pid) xexec(cmd->argv);
		else waitpid(cmd->pid, &status, 0);

		if (CFG_TOYSH_FLOWCTL || CFG_TOYSH_PIPES) {
			if (WIFEXITED(status)) cmd->pid = WEXITSTATUS(status);
			if (WIFSIGNALED(status)) cmd->pid = WTERMSIG(status);
		}
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
		llist_free(line.cmd, free_cmd);
	}
}

void cd_main(void)
{
	char *dest = *toys.optargs ? *toys.optargs : getenv("HOME");
	xchdir(dest);
}

void exit_main(void)
{
	exit(*toys.optargs ? atoi(*toys.optargs) : 0);
}

void toysh_main(void)
{
	FILE *f;

	// Set up signal handlers and grab control of this tty.
	if (CFG_TOYSH_TTY) {
		if (isatty(0)) toys.optflags |= 1;
	}
	f = *toys.optargs ? xfopen(*toys.optargs, "r") : NULL;
	if (TT.command) handle(TT.command);
	else {
		size_t cmdlen = 0;
		for (;;) {
			char *command = 0;
			if (!f) xputc('$');
			if (1 > getline(&command, &cmdlen, f ? : stdin)) break;
			handle(command);
			free(command);
		}
	}

	toys.exitval = 1;
}
