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

USE_SH(NEWTOY(cd, NULL, TOYFLAG_NOFORK))
USE_SH(NEWTOY(exit, NULL, TOYFLAG_NOFORK))

USE_SH(NEWTOY(sh, "c:i", TOYFLAG_BIN))
USE_SH(OLDTOY(toysh, sh, TOYFLAG_BIN))
// Login lies in argv[0], so add some aliases to catch that
USE_SH(OLDTOY(-sh, sh, 0))
USE_SH(OLDTOY(-toysh, sh, 0))

config SH
  bool "sh (toysh)"
  default n
  help
    usage: sh [-c command] [script]

    Command shell.  Runs a shell script, or reads input interactively
    and responds to it.

    -c	command line to execute
    -i	interactive mode (default when STDIN is a tty)

# These are here for the help text, they're not selectable and control nothing
config CD
  bool
  default n
  depends on SH
  help
    usage: cd [-PL] [path]

    Change current directory.  With no arguments, go $HOME.

    -P	Physical path: resolve symlinks in path
    -L	Local path: .. trims directories off $PWD (default)

config EXIT
  bool
  default n
  depends on SH
  help
    usage: exit [status]

    Exit shell.  If no return value supplied on command line, use value
    of most recent command, or 0 if none.
*/

#define FOR_sh
#include "toys.h"

GLOBALS(
  char *command;

  long lineno;

  // parse scratch space
  struct double_list *parse;

  // Running jobs.
  struct sh_job {
    struct sh_job *next, *prev;
    unsigned jobno;

    // Every pipeline has at least one set of arguments or it's Not A Thing
    struct sh_arg {
      char **v;
      unsigned long c;
    } pipeline;

    // null terminated array of running processes in pipeline
    struct sh_process {
      struct string_list *delete; // expanded strings
      int pid, exit;   // status? Stopped? Exited?
      char *end;
      struct sh_arg arg;
    } *procs, *proc;
  } *jobs, *job;
  unsigned jobcnt;
)

void cd_main(void)
{
  char *dest = *toys.optargs ? *toys.optargs : getenv("HOME");

  xchdir(dest ? dest : "/");
}

void exit_main(void)
{
  exit(*toys.optargs ? atoi(*toys.optargs) : 0);
}

// Print prompt, parsing escapes
static void do_prompt(char *prompt)
{
  char *s, c, cc;

  if (!prompt) prompt = "\\$ ";
  while (*prompt) {
    c = *(prompt++);

    if (c=='!') {
      if (*prompt=='!') prompt++;
      else {
        printf("%ld", TT.lineno);
        continue;
      }
    } else if (c=='\\') {
      int i = 0;

      cc = *(prompt++);
      if (!cc) goto down;

      // \nnn \dD{}hHjlstT@AuvVwW!#$
      // Ignore bash's "nonprintable" hack; query our cursor position instead.
      if (cc=='[' || cc==']') continue;
      else if (cc=='$') putchar(getuid() ? '$' : '#');
      else if (cc=='h' || cc=='H') {
        *toybuf = 0;
        gethostname(toybuf, sizeof(toybuf)-1);
        if (cc=='h' && (s = strchr(toybuf, '.'))) *s = 0;
        fputs(toybuf, stdout);
      } else if (cc=='s') fputs(getbasename(*toys.argv), stdout);
      else {
        if (!(c = unescape(cc))) {
          c = '\\';
          prompt--;
        }
        i++;
      }
      if (!i) continue;
    }
down:
    putchar(c);
  }
  fflush(stdout);
}

// Execute the commands in a pipeline
static void run_command(struct sh_process *pp)
{
  struct toy_list *tl = toy_find(*pp->arg.v);

  // Is this command a builtin that should run in this process?
  if (tl && (tl->flags & TOYFLAG_NOFORK)) {
    struct toy_context temp;
    sigjmp_buf rebound;

    // This fakes lots of what toybox_main() does.
    memcpy(&temp, &toys, sizeof(struct toy_context));
    memset(&toys, 0, sizeof(struct toy_context));

    if (!sigsetjmp(rebound, 1)) {
      toys.rebound = &rebound;
      toy_init(tl, pp->arg.v);
      tl->toy_main();
    }
    pp->exit = toys.exitval;
    if (toys.optargs != toys.argv+1) free(toys.optargs);
    if (toys.old_umask) umask(toys.old_umask);
    memcpy(&toys, &temp, sizeof(struct toy_context));
  } else {
    int pipe[2];

    pipe[0] = 0;
    pipe[1] = 1;
    if (-1 == (pp->pid = xpopen_both(pp->arg.v, pipe)))
      perror_msg("%s: not found", *pp->arg.v);
    else pp->exit = xpclose_both(pp->pid, 0);
  }

  return;
}

// todo: ${name:?error} causes an error/abort here (syntax_err longjmp?)
static void expand_arg(struct sh_arg *arg, char *new)
{
  if (!(arg->c&32)) arg->v = xrealloc(arg->v, sizeof(void *)*(arg->c+33));

  arg->v[arg->c++] = new;
  arg->v[arg->c] = 0;
}

// like error_msg() but exit from shell scripts
void syntax_err(char *msg, ...)
{
  va_list va;

  va_start(va, msg);
  verror_msg(msg, 0, va);
  va_end(va);

  if (*toys.optargs) xexit();
}


// Parse one word from the command line, appending one or more argv[] entries
// to struct command.  Handles environment variable substitution and
// substrings.  Returns pointer to next used byte, or NULL if it
// hit an ending token.

// caller eats leading spaces

// parse next word from command line. Returns end, or 0 if need continuation
static char *parse_word(char *start)
{
  int i, quote = 0;
  char *end = start, *s;

  // find end of string

  while (*end) {
    i = 0;

    // Handle quote contexts
    if (quote) {
      // end quote, skip quoted chars
      if (*end == toybuf[quote-1]) quote--, end++;
      else if (toybuf[quote-1] == '"' && *end == '`') toybuf[quote++] = *end++;
      else if (toybuf[quote-1] == '\'' || isspace(*end)) end++;
      else i++;
    } else {
      if (isspace(*end)) break;
      // start quote
      if (strchr("\"'`", *end)) toybuf[quote++] = *end++;
      else if (strstart(&end, "<(") || strstart(&end,">(")) toybuf[quote++]=')';
      else if (*end==')') return end+(end==start);
      else {
        // control chars
        for (s = end; strchr(";|&<>(", *s); s++);
        if (s != end) return (end == start) ? s : end;
        i++;
      }
    }

    // loop if we already handled a symbol
    if (!i) continue;

    // Things the same unquoted or in double quotes

    // backslash escapes
    if (*end == '\\') {
      if (!end[1]) return 0;
      end += 2;
    } else if (*end == '$') {
      // barf if we're near overloading quote stack (nesting ridiculously deep)
      if (quote>4000) {
        syntax_err("tilt");
        return (void *)1;
      }
      end++;
      if (strstart(&end, "((")) {
        // all we care about here are parentheses matching and then ending ))
        for (i = 0;;) {
          if (!*end) return 0;
          if (!i && strstart(&end, "))")) break;
          if (*end == '(') i++;
          else if (*end == ')') i--;
        }
      } else if (-1 != (i = stridx("({[", *end))) {
        toybuf[quote++] = ")}]"[i];
        end++;
      }
    } else end++;
  }

  return quote ? 0 : end;
}

// Consume a line of shell script and do what it says. Returns 0 if finished,
// pointer to start of unused part of line if it needs another line of input.
static char *parse_line(char *line, struct double_list **pipeline)
{
  char *start = line, *end, *s;
  struct sh_arg *arg = 0;
  struct double_list *pl, *expect = 0;
  unsigned i, paren = 0;

  // Resume appending to last pipeline's last argument list
  if (*pipeline) arg = (void *)(*pipeline)->prev->data;
  if (arg) for (i = 0; i<arg->c; i++) {
    if (!strcmp(arg->v[i], "(")) paren++;
    else if (!strcmp(arg->v[i], ")")) paren--;
  }

  // Loop handling each word
  for (;;) {
    // Skip leading whitespace/comment
    while (isspace(*start)) ++start;
    if (*start=='#') {
      while (*start && *start != '\n') start++;
      continue;
    }

    // Parse next word and detect continuation/overflow.
    if ((end = parse_word(start)) == (void *)1) return 0;
    if (!end) return start;

    // Extend pipeline and argv[], handle EOL
    if (!arg)
      dlist_add(pipeline, (void *)(arg = xzalloc(sizeof(struct sh_arg))));
    if (!(31&arg->c)) arg->v = xrealloc(arg->v, (32+arg->c)*sizeof(void *));
    if (end == start) {
      arg->v[arg->c] = 0;
      break;
    }

    // Save argument (strdup) and check if it's special
    s = arg->v[arg->c] = xstrndup(start, end-start);
    if (!strcmp(s, "(")) paren++;
    else if (!strcmp(s, ")") && !paren--) syntax_err("bad %s", s);
    if (paren || !strchr(";|&", *start)) arg->c++;
    else {
      if (!arg->c) {
        syntax_err("bad %s", arg->v[arg->c]);
        goto flush;
      }
      arg = 0;
    }
    start = end;
  }

  // We parsed to the end of the line, which ended a pipeline.
  // Now handle flow control commands, which can also need more lines.

  // array of command lines separated by | and such
  // Note: don't preparse past ; because environment variables differ

  // Check for flow control continuations
  end = 0;
  for (pl = *pipeline; pl ; pl = (pl->next == *pipeline) ? 0 : pl->next) {
    arg = (void *)pl->data;
    if (!arg->c) continue;

    // parse flow control statements in this command line
    for (i = 0; i<arg->c; i++) {
      char *ex = expect ? expect->prev->data : 0;

      s = arg->v[i];
      if (!strcmp(s, "if")) ex = "then";
      else if (!strcmp(s, "for") || !strcmp(s, "select")
          || !strcmp(s, "while") || !strcmp(s, "until")) ex = "do";
      else if (!strcmp(s, "case")) ex = "esac";
      else if (!strcmp(s, "{")) ex = "}";
      else if (!strcmp(s, "[[")) ex = "]]";

      // If we expect a non-flow-control command, eat rest of line
      else if (expect && !ex) {
        free(dlist_pop(&expect));
        continue;

      // Did we find a specific word we were waiting for?
      } else if (ex && !strcmp(arg->v[i], ex)) {
        free(dlist_pop(&expect));
        if (end && !strcmp(end, ";")) {
          // can't if | then or while && do, only ; or newline counts
          syntax_err("bad %s", end);
          goto flush;
        }
        if (!strcmp(s, "do")) dlist_add(&expect, "done");
        else if (!strcmp(s, "then")) dlist_add(&expect, "fi\0A");
        break;
      // fi could have elif, which queues a then.
      } else if (ex && !strcmp(ex, "fi")) {
        if (!strcmp(s, "elif")) {
          free(dlist_pop(&expect));
          dlist_add(&expect, "then");
        // catch duplicate else while we're here
        } else if (!strcmp(s, "else")) {
          if (ex[3] != 'A') {
            syntax_err("2 else"); 
            goto flush;
          }
          free(dlist_pop(&expect));
          dlist_add(&expect, "fi\0B");
        }
      } else break;

      dlist_add(&expect, ex);
    }
    // Record how the previous stanza ended
    end = arg->v[arg->c];
  }

  // If we need more lines to finish flow control...
  // TODO: functions
  if (expect) {
    llist_traverse(expect, free);
    return start;
  }

  for (pl = *pipeline; pl ; pl = (pl->next == *pipeline) ? 0 : pl->next) {
    struct sh_process *pp = xzalloc(sizeof(struct sh_process));

    for (i = 0; i<((struct sh_arg *)pl->data)->c; i++)
      expand_arg(&pp->arg, ((struct sh_arg *)pl->data)->v[i]);
    run_command(pp);
  }

flush:
  while ((pl = dlist_pop(pipeline))) {
    arg = (void *)pl->data;
    free(pl);
    for (i = 0; i<arg->c; i++) free(arg->v[i]);
    free(arg->v);
    free(arg);
  }
  *pipeline = 0;

  return 0;
}

void sh_main(void)
{
  FILE *f = 0;
  char *command = 0, *old = 0;
  struct double_list *scratch = 0;

  // Set up signal handlers and grab control of this tty.
  if (isatty(0)) toys.optflags |= FLAG_i;

  if (*toys.optargs) f = xfopen(*toys.optargs, "r");
  if (TT.command) command = parse_line(TT.command, &scratch);
  else for (;;) {
    char *new = 0;
    size_t linelen = 0;

    // Prompt and read line
    if (!f) do_prompt(getenv(command ? "PS2" : "PS1"));
    if (1 > getline(&new, &linelen, f ? f : stdin)) break;
    if (f) TT.lineno++;

    // Append to unused portion of previous line if any
    if (command) {
      command = xmprintf("%s%s", command, new);
      free(old);
      free(new);
      old = command;
    } else {
      free(old);
      old = new;
    }

    // returns 0 if line consumed, command if it needs more data
    command = parse_line(old, &scratch);
  }

  if (command) error_exit("unfinished line");
  toys.exitval = f && ferror(f);
}
