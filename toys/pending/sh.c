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
      perror_msg("%s: vfork", *pp->arg.v);
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

// return length of match found at this point
static int anyof(char *s, char **try)
{
  while (*try) {
    if (strstart(&s, *try)) return strlen(*try);
    try++;
  }

  return 0;
}

// parse next word from command line. Returns end, or 0 if need continuation
// caller eats leading spaces
static char *parse_word(char *start)
{
  int i, quote = 0;
  char *end = start, *s;

  // Skip leading whitespace/comment
  for (;;) {
    if (isspace(*start)) ++start;
    else if (*start=='#') while (*start && *start != '\n') ++start;
    else break;
  }

  // find end of this word
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
      else {
        // control chars.
        // 123<<file- parses as 2 args: "123<<" "file-".
        // note: >&; becomes ">&" ";" because first loop, then second loop.
        s = end;
        if (*s == '{') s++;
        for (s = end; isdigit(*s); s++);
        if (*end == '{' && *s == '}') s++;
        s += anyof(s, (char *[]){"<<<", "<<-", "<<", "<&", "<>", "<", ">>",
          ">&", ">|", ">", 0});
        if (s == end || isdigit(s[-1]))
          s += anyof(s, (char *[]){";;&", ";;", ";&", ";", "||", "|&", "|",
            "&&", "&>>", "&>", "&", "(", ")", 0});
        if (s != end && !isdigit(*s)) return (end == start) ? s : end;
        i++;
      }
    }

    // loop if we already handled a symbol
    if (!i) continue;

    // Things the same unquoted or in double quotes

    // backslash escapes
    if (*end == '\\') {
      if (!end[1] || (end[1]=='\n' && !end[2])) return 0;
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

// Parse flow control statement(s), returns index of first statement to execute,
// pp->arg->c if none, -1 if we need to flush due to syntax error
int flow_control(int why, struct sh_arg *arg, struct double_list **expect,
  char **end)
{
  char *add = 0;
  int i, pend = 0;

  // Blank line shouldn't change end, but two ends in a row are an error
  if (!arg->c) {
    if (arg->v[0]) {
      syntax_err("bad %s", arg->v[0]);
      return -1;
    }
    return 0;
  }

  // parse flow control statements in this command line
  for (i = 0; ; i++) {
    char *ex = *expect ? (*expect)->prev->data : 0, *s = arg->v[i];

    // push word to expect at end of block, and expect a command first
    if (add) {
      dlist_add(expect, add);                  // end of context
      if (why) dlist_add(expect, arg->v[i-1]); // context for command
      dlist_add(expect, add = 0);              // expect a command
    }

    // end of argument list?
    if (i == arg->c) break;

    // When waiting for { it must be next symbol, but can be on a new line.
    if (ex && !strcmp(ex, "{")) {
      if (strcmp(s, "{") || (!i && *end && strcmp(*end, ";"))) {
        syntax_err("need {");
        return -1;
      }
    }

    if (!strcmp(s, "if")) add = "then";
    else if (!strcmp(s, "for") || !strcmp(s, "select")
        || !strcmp(s, "while") || !strcmp(s, "until")) add = "do";
    else if (!strcmp(s, "case")) add = "esac";
    else if (!strcmp(s, "{")) add = "}";
    else if (!strcmp(s, "[[")) add = "]]";
    else if (!strcmp(s, "(")) add = ")";

    // function NAME () [nl] { [nl] body ; }
    // Why can you to declare functions inside other functions?
    else if (arg->c>i+1 && !strcmp(arg->v[i+1], "(")) goto funky;
    else if (!strcmp(s, "function")) {
      i++;
funky:
      // At this point we can only have a function: barf if it's invalid
      if (arg->c<i+3 || !strcmp(arg->v[i+1], "(") || !strcmp(arg->v[i+2], ")")){
        syntax_err("bad function ()");
        return -1;
      }
      // perform abnormal add (one extra piece of info) manually.
      dlist_add(expect, "}");
      dlist_add(expect, "function");
      dlist_add(expect, 0);
      dlist_add(expect, "{");

      continue;

    // Expecting NULL means a statement: any otherwise unrecognized word
    } else if (expect && !ex) {
      free(dlist_pop(expect));

      // if (why) context in which statement executes now at top of expect stack

      // Does this statement end with a close parentheses?
      if (!strcmp(")", arg->v[arg->c-1])) {

        // Did we expect one?
        if (!*expect || !strcmp(")", (*expect)->prev->data)) {
          syntax_err("bad %s", ")");
          return -1;
        }

        free(dlist_pop(expect));
        // only need one statement in ( ( ( echo ) ) )
        if (*expect && !(*expect)->prev->data) free(dlist_pop(expect));

        pend++;
        goto gotparen;
      }
      break;

    // If we aren't expecting and didn't just start a new flow control block,
    // rest of statement is a command and arguments, so stop now
    } else if (!ex) break;

    if (add) continue;

    // If we got here we expect a specific word to end this block: is this it?
    if (!strcmp(arg->v[i], ex)
      || (!strcmp(ex, ")") && !strcmp(ex, arg->v[arg->c-1])))
    {
      // can't "if | then" or "while && do", only ; & or newline works
      if (*end && strcmp(*end, ";") && strcmp(*end, "&")) {
        syntax_err("bad %s", *end);
        return -1;
      }

gotparen:
      free(dlist_pop(expect));
      // Only innermost statement needed in { { { echo ;} ;} ;} and such
      if (*expect && !(*expect)->prev->data) free(dlist_pop(expect));

      // If this was a command ending in parentheses
      if (pend) break;

      // if it's a multipart block, what comes next?
      if (!strcmp(s, "do")) ex = "done";
      else if (!strcmp(s, "then")) add = "fi\0A";
    // fi could have elif, which queues a then.
    } else if (!strcmp(ex, "fi")) {
      if (!strcmp(s, "elif")) {
        free(dlist_pop(expect));
        add = "then";
      // catch duplicate else while we're here
      } else if (!strcmp(s, "else")) {
        if (ex[3] != 'A') {
          syntax_err("2 else");
          return -1;
        }
        free(dlist_pop(expect));
        add = "fi\0B";
      }
    }
  }

  // Record how the previous stanza ended: ; | & ;; || && ;& ;;& |& NULL
  *end = arg->v[arg->c];

  return i;
}

// Consume a line of shell script and do what it says. Returns 0 if finished,
// 1 to request another line of input.

struct sh_parse {
  struct double_list *pipeline, *plstart, *expect, *here;
  char *end;
};

// pipeline and expect are scratch space, state held between calls which
// I don't want to make global yet because this could be reentrant.
// returns 1 to request another line (> prompt), 0 if line consumed.
static int parse_line(char *line, struct sh_parse *sp)
{
  char *start = line, *delete = 0, *end, *s;
  struct sh_arg *arg = 0;
  struct double_list *pl;
  long i;

  // Resume appending to last statement?
  if (sp->pipeline) {
    arg = (void *)sp->pipeline->prev->data;

    // Extend/resume quoted block
    if (arg->c<0) {
      start = delete = xmprintf("%s%s", arg->v[arg->c = (-arg->c)-1], start);
      free(arg->v[arg->c]);
      arg->v[arg->c] = 0;

    // is a HERE document in progress?
    } else if (sp->here && ((struct sh_arg *)sp->here->data)->c<0) {
      unsigned long c;

      arg = (void *)sp->here->data;
      c = -arg->c - 1;

      // HERE's arg->c < 0 means still adding to it, EOF string is last entry
      if (!(31&c)) arg->v = xrealloc(arg->v, (32+c)*sizeof(void *));
      if (strcmp(line, arg->v[c])) {
        // Add this line
        arg->v[c+1] = arg->v[c];
        arg->v[c] = xstrdup(line);
        arg->c--;
      } else {
        // EOF hit, end HERE document
        arg->v[arg->c = c] = 0;
        sp->here = sp->here->next;
      }
      start = 0;
    }
  }

  // Parse words, assemble argv[] pipelines, check flow control and HERE docs
  if (start) for (;;) {
    s = 0;

    // Parse next word and detect overflow (too many nested quotes).
    if ((end = parse_word(start)) == (void *)1) goto flush;

    // Extend pipeline and argv[] to store result
    if (!arg)
      dlist_add(&sp->pipeline, (void *)(arg = xzalloc(sizeof(struct sh_arg))));
    if (!(31&arg->c)) arg->v = xrealloc(arg->v, (32+arg->c)*sizeof(void *));

    // Do we need to request another line to finish word (find ending quote)?
    if (!end) {
      // Save unparsed bit of this line, we'll need to re-parse it.
      arg->v[arg->c] = xstrndup(start, strlen(start));
      arg->c = -(arg->c+1);
      free(delete);

      return 1;
    }

    // Did we hit the end of this line of input?
    if (end == start) {
      arg->v[arg->c] = 0;

      // Parse flow control data from last statement
      if (-1 == flow_control(0, arg, &sp->expect, &sp->end)) goto flush;

      // Grab HERE document(s)
      for (pl = sp->plstart ? sp->plstart : sp->pipeline; pl;
           pl = (pl->next == sp->pipeline) ? 0 : pl->next)
      {
        struct sh_arg *here;

        arg = (void *)pl->data;

        for (i = 0; i<arg->c; i++) {
          // find [n]<<[-] with an argument after it
          s = arg->v[i];
          if (*s == '{') s++;
          while (isdigit(*s)) s++;
          if (*arg->v[i] == '{' && *s == '}') s++;
          if (strcmp(s, "<<") && strcmp(s, "<<-")) continue;
          if (i+1 == arg->c) goto flush;

          here = xzalloc(sizeof(struct sh_arg));
          here->v = xzalloc(32*sizeof(void *));
          *here->v = arg->v[++i];
          here->c = -1;
        }
      }

      // Stop reading.
      break;
    }

    // ) only saved at start of a statement, else ends statement with NULL
    if (arg->c && *start == ')') {
      arg->v[arg->c] = 0;
      end--;
      if (-1 == flow_control(0, arg, &sp->expect, &sp->end)) goto flush;
      arg = 0;
    } else {
      // Save argument (strdup) and check if it's special
      s = arg->v[arg->c] = xstrndup(start, end-start);
      if (!strchr(");|&", *start)) arg->c++;
      else {
        // end of statement due to flow control character.
        s = 0;
        if (!arg->c) goto flush;
        if (-1 == flow_control(0, arg, &sp->expect, &sp->end)) goto flush;
        arg = 0;
      }
    }
    start = end;
  }
  free(delete);

  // return if HERE document or more flow control
  if (sp->expect || (sp->pipeline && sp->pipeline->prev->data==(void *)1))
    return 1;

  // At this point, we've don't need more input and can start executing.

  // **************************** do the thing *******************************

  // Now we have a complete thought and can start running stuff.

  // iterate through the commands running each one

  // run a pipeline of commands

  for (pl = sp->pipeline; pl ; pl = (pl->next == sp->pipeline) ? 0 : pl->next) {
    struct sh_process *pp = xzalloc(sizeof(struct sh_process));

    for (i = 0; i<arg->c; i++) expand_arg(&pp->arg, arg->v[i]);
    run_command(pp);
    llist_traverse(pp->delete, free);
  }

  s = 0;
flush:

  if (s) syntax_err("bad %s", s);
  while ((pl = dlist_pop(&sp->pipeline))) {
    arg = (void *)pl->data;
    free(pl);
    for (i = 0; i<arg->c; i++) free(arg->v[i]);
    free(arg->v);
    free(arg);
  }

  while ((pl = dlist_pop(&sp->here))) {
    arg = (void *)pl->data;
    free(pl);
    if (arg->c<0) arg->c = -arg->c - 1;
    for (i = 0; i<arg->c; i++) free(arg->v[i]);
    free(arg->v);
    free(arg);
  }

  llist_traverse(sp->expect, free);

  return 0;
}

void sh_main(void)
{
  FILE *f;
  struct sh_parse scratch;
  int prompt = 0;

  // Set up signal handlers and grab control of this tty.

  memset(&scratch, 0, sizeof(scratch));
  if (TT.command) f = fmemopen(TT.command, strlen(TT.command), "r");
  else if (*toys.optargs) f = xfopen(*toys.optargs, "r");
  else {
    f = stdin;
    if (isatty(0)) toys.optflags |= FLAG_i;
  }

  for (;;) {
    char *new = 0;
    size_t linelen = 0;

    // Prompt and read line
    if (f == stdin) {
      char *s = getenv(prompt ? "PS2" : "PS1");

      if (!s) s = prompt ? "> " : (getpid() ? "\\$ " : "# ");
      do_prompt(s);
    } else TT.lineno++;
    if (1>(linelen = getline(&new, &linelen, f ? f : stdin))) break;
    if (new[linelen-1] == '\n') new[--linelen] = 0;

    // returns 0 if line consumed, command if it needs more data
    prompt = parse_line(new, &scratch);
    free(new);
  }

  if (prompt) error_exit("%ld:unfinished line"+4*!TT.lineno, TT.lineno);
  toys.exitval = f && ferror(f);
}
