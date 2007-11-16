/* vi: set sw=4 ts=4:
 *
 * toysh - toybox shell
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * The spec for this is at:
 * http://www.opengroup.org/onlinepubs/009695399/utilities/xcu_chap02.html
 *
 * Although things like the bash man page are good to read too.
 */

// Handle embedded NUL bytes in the command line.

#include "toys.h"

#define TT toy.toysh

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
		cmd->pid = tl->toy_main();
		free(toys.optargs);
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

int cd_main(void)
{
	char *dest = *toys.optargs ? *toys.optargs : getenv("HOME");
	if (chdir(dest)) error_exit("chdir %s",dest);
	return 0;
}

int exit_main(void)
{
	exit(*toys.optargs ? atoi(*toys.optargs) : 0);
}

int toysh_main(void)
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
			if (!f) putchar('$');
			if (1 > getline(&command, &cmdlen, f ? : stdin)) break;
			handle(command);
			free(command);
		}
	}

	return 1;
}
