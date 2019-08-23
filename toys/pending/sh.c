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
 * TODO: "make sh" doesn't work (nofork builtins need to be included)
 * TODO: test that $PS1 color changes work without stupid \[ \] hack
 * TODO: make fake pty wrapper for test infrastructure
 * TODO: // Handle embedded NUL bytes in the command line.
 * TODO: var=val command
 * existing but considered builtins: false kill pwd true
 * buitins: alias bg command fc fg getopts jobs newgrp read umask unalias wait
 * "special" builtins: break continue : . eval exec export readonly return set
 *   shift times trap unset
 * | & ; < > ( ) $ ` \ " ' <space> <tab> <newline>
 * * ? [ # ~ = %
 * ! { } case do done elif else esac fi for if in then until while
 * [[ ]] function select
 * $@ $* $# $? $- $$ $! $0
 * ENV HOME IFS LANG LC_ALL LINENO PATH PPID PS1 PS2 PS4 PWD
 * label:
 * TODO: test exit from "trap EXIT" doesn't recurse
 * TODO: ! history expansion
 *
 * bash man page:
 * control operators || & && ; ;; ;& ;;& ( ) | |& <newline>
 * reserved words
 *   ! case  coproc  do done elif else esac fi for  function  if  in  select
 *   then until while { } time [[ ]]



USE_SH(NEWTOY(cd, NULL, TOYFLAG_NOFORK))
USE_SH(NEWTOY(exit, NULL, TOYFLAG_NOFORK))

USE_SH(NEWTOY(sh, "c:i", TOYFLAG_BIN))
USE_SH(OLDTOY(toysh, sh, TOYFLAG_BIN))
// Login lies in argv[0], so add some aliases to catch that
USE_SH(OLDTOY(-sh, sh, 0))
USE_SH(OLDTOY(-toysh, sh, 0))

config SH
  bool "sh (toysh)"
  default y
  help
    usage: sh [-c command] [script]

    Command shell.  Runs a shell script, or reads input interactively
    and responds to it.

    -c	command line to execute
    -i	interactive mode (default when STDIN is a tty)

# These are here for the help text, they're not selectable and control nothing
config CD
  bool
  default y
  depends on SH
  help
    usage: cd [-PL] [path]

    Change current directory.  With no arguments, go $HOME.

    -P	Physical path: resolve symlinks in path
    -L	Local path: .. trims directories off $PWD (default)

config EXIT
  bool
  default y
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

  struct double_list functions;
  unsigned options;

  // Running jobs.
  struct sh_job {
    struct sh_job *next, *prev;
    unsigned jobno;

    // Every pipeline has at least one set of arguments or it's Not A Thing
    struct sh_arg {
      char **v;
      int c;
    } pipeline;

    // null terminated array of running processes in pipeline
    struct sh_process {
      struct string_list *delete; // expanded strings
      int pid, exit;   // status? Stopped? Exited?
      struct sh_arg arg;
    } *procs, *proc;
  } *jobs, *job;
  unsigned jobcnt;
)

#define SH_NOCLOBBER 1   // set -C

void cd_main(void)
{
  char *dest = *toys.optargs ? *toys.optargs : getenv("HOME");

  xchdir(dest ? dest : "/");
}

void exit_main(void)
{
  exit(*toys.optargs ? atoi(*toys.optargs) : 0);
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

// quote removal, brace, tilde, parameter/variable, $(command),
// $((arithmetic)), split, path 
#define NO_PATH  (1<<0)
#define NO_SPLIT (1<<1)
// todo: ${name:?error} causes an error/abort here (syntax_err longjmp?)
// todo: $1 $@ $* need args marshalled down here: function+structure?
// arg = append to this
// new = string to expand
// flags = type of expansions (not) to do
// delete = append new allocations to this so they can be freed later
// TODO: at_args: $1 $2 $3 $* $@
static void expand_arg(struct sh_arg *arg, char *new, unsigned flags,
  struct string_list **delete)
{
  if (!(arg->c&32)) arg->v = xrealloc(arg->v, sizeof(void *)*(arg->c+33));

  arg->v[arg->c++] = new;
  arg->v[arg->c] = 0;

/*
  char *s = word, *new = 0;

  // replacement
  while (*s) {
    if (*s == '$') {
      s++;
    } else if (*strchr("*?[{", *s)) {
      s++;
    } else if (*s == '<' || *s == '>') {
      s++;
    } else s++;
  }

  return new;
*/
}

// Assign one variable
// s: key=val
// type: 0 = whatever it was before, local otherwise
#define TAKE_MEM 0x80000000
// declare -aAilnrux
// ft
void setvar(char *s, unsigned type)
{
  if (type&TAKE_MEM) type ^= TAKE_MEM;
  else s = xstrdup(s);

  // local, export, readonly, integer...
  xsetenv(s, 0);
}

char *getvar(char *s)
{
  return getenv(s);
}

// return length of match found at this point
static int anystart(char *s, char **try)
{
  while (*try) {
    if (strstart(&s, *try)) return strlen(*try);
    try++;
  }

  return 0;
}

static int anystr(char *s, char **try)
{
  while (*try) if (!strcmp(s, *try++)) return 1;

  return 0;
}

// return length of valid prefix that could go before redirect
int redir_prefix(char *word)
{
  char *s = word;

  if (*s == '{') {
    for (s++; isalnum(*s) || *s=='_'; s++);
    if (*s == '}' && s != word+1) s++;
    else s = word;
  } else while (isdigit(*s)) s++;

  return s-word;
}

// todo |&

// rd[0] = next, 1 = prev, 2 = len, 3-x = to/from redirection pairs.
// Execute the commands in a pipeline segment
struct sh_process *run_command(struct sh_arg *arg, int **rdlist)
{
  struct sh_process *pp = xzalloc(sizeof(struct sh_process));
  struct toy_list *tl;
  char *s, *ss, *sss;
  unsigned envlen, j;
  int fd, here = 0, rdcount = 0, *rd = 0, *rr, hfd = 0;

  // Grab variable assignments
  for (envlen = 0; envlen<arg->c; envlen++) {
    s = arg->v[envlen];
    for (j=0; s[j] && (s[j]=='_' || !ispunct(s[j])); j++);
    if (!j || s[j] != '=') break;
  }

  // perform assignments locally if there's no command
  if (envlen == arg->c) {
    for (j = 0; j<envlen; j++) {
      struct sh_arg aa;

      aa.c = 0;
      expand_arg(&aa, arg->v[j], NO_PATH|NO_SPLIT, 0);
      setvar(*aa.v, TAKE_MEM);
      free(aa.v);
    }
    free(pp);

    return 0;
  }

  // We vfork() instead of fork to support nommu systems, and do
  // redirection setup in the parent process. Open new filehandles
  // and move them to temporary values >10. The rd[] array has pairs of
  // filehandles: child replaces fd1 with fd2 via dup2() and close() after
  // the vfork(). fd2 is <<1, if bottom bit set don't close it (dup instead).
  // If fd2 < 0 it's a here document (parent process writes to a pipe later).

  // Expand arguments and perform redirections
  for (j = envlen; j<arg->c; j++) {

    // Is this a redirect?
    ss = (s = arg->v[j]) + redir_prefix(arg->v[j]);
    if (!anystr(ss, (char *[]){"<<<", "<<-", "<<", "<&", "<>", "<", ">>", ">&",
      ">|", ">", "&>>", "&>", 0}))
    {
      // Nope: save/expand argument and loop
      expand_arg(&pp->arg, s, 0, 0);

      continue;
    }

    // Yes. Expand rd[] and find first unused filehandle >10
    if (!(rdcount&31)) {
      if (rd) dlist_lpop((void *)rdlist);
      rd = xrealloc(rd, (2*rdcount+3+2*32)*sizeof(int *));
      dlist_add_nomalloc((void *)rdlist, (void *)rd);
    }
    rr = rd+3+rdcount;
    if (!hfd)
      for (hfd = 10; hfd<99999; hfd++) if (-1 == fcntl(hfd, F_GETFL)) break;

    // error check: premature EOF, target fd too high, or redirect file splits
    if (++j == arg->c || (isdigit(*s) && ss-s>5)) goto flush;
    fd = pp->arg.c;

    // expand arguments for everything but << and <<-
    if (strncmp(ss, "<<", 2) || ss[2] == '<') {
      expand_arg(&pp->arg, arg->v[j], NO_PATH|(NO_SPLIT*!strcmp(ss, "<<<")), 0);
      if (fd+1 != pp->arg.c) goto flush;
      sss = pp->arg.v[--pp->arg.c];
    } else dlist_add((void *)&pp->delete, sss = xstrdup(arg->v[j]));

    // rd[] entries come in pairs: first is which fd gets redirected after
    // vfork(), I.E. the [n] part of [n]<word

    if (isdigit(*ss)) fd = atoi(ss);
    else if (*ss == '{') {
      ss++;
      // when we close a filehandle, we _read_ from {var}, not write to it
      if ((!strcmp(s, "<&") || !strcmp(s, ">&")) && !strcmp(sss, "-")) {
        ss = xstrndup(ss, (s-ss)-1);
        sss = getvar(ss);
        free(ss);
        fd = -1;
        if (sss) fd = atoi(sss);
        if (fd<0) goto flush;
        if (fd>2) {
          rr[0] = fd;
          rr[1] = fd<<1; // close it
          rdcount++;
        }
        continue;
      } else setvar(xmprintf("%.*s=%d", (int)(s-ss), ss, hfd),  TAKE_MEM); 
    } else fd = *ss != '<';
    *rr = fd;

    // at this point for [n]<word s = start of [n], ss = start of <, sss = word

    // second entry in this rd[] pair is new fd to dup2() after vfork(),
    // I.E. for [n]<word the fd if you open("word"). It's stored <<1 and the
    // low bit set means don't close(rr[1]) after dup2(rr[1]>>1, rr[0]);

    // fd<0 means HERE document. Canned input stored earlier, becomes pipe later
    if (!strcmp(s, "<<<") || !strcmp(s, "<<-") || !strcmp(s, "<<")) {
      fd = --here<<2;
      if (s[2] == '-') fd += 1;          // zap tabs
      if (s[strcspn(s, "\"'")]) fd += 2; // it was quoted so no expansion
      rr[1] = fd;
      rdcount++;

      continue;
    }

    // Handle file descriptor duplication/close (&> &>> <& >& with number or -)
    if (strchr(ss, '&') && ss[2] != '>') {
      char *dig = sss;

      // These redirect existing fd so nothing to open()
      while (isdigit(dig)) dig++;
      if (dig-sss>5) {
        s = sss;
        goto flush;
      }

// TODO can't check if fd is open here, must do it when actual redirects happen
      if (!*dig || (*dig=='-' && !dig[1])) {
        rr[1] = (((dig==sss) ? *rr : atoi(sss))<<1)+(*dig != '-');
        rdcount++;

        continue;
      }
    }

    // Permissions to open external file with: < > >> <& >& <> >| &>> &>
    if (!strcmp(ss, "<>")) fd = O_CREAT|O_RDWR;
    else if (strstr(ss, ">>")) fd = O_CREAT|O_APPEND;
    else {
      fd = (*ss != '<') ? O_CREAT|O_WRONLY|O_TRUNC : O_RDONLY;
      if (!strcmp(ss, ">") && (TT.options&SH_NOCLOBBER)) {
        struct stat st;

        // Not _just_ O_EXCL: > /dev/null allowed
        if (stat(sss, &st) || !S_ISREG(st.st_mode)) fd |= O_EXCL;
      }
    }

    // Open the file
// TODO: /dev/fd/# /dev/{stdin,stdout,stderr} /dev/{tcp,udp}/host/port
    if (-1 == (fd = xcreate(sss, fd|WARN_ONLY, 777)) || hfd != dup2(fd, hfd)) {
      pp->exit = 1;
      s = 0;

      goto flush;
    }
    if (fd != hfd) close(fd);
    rr[1] = hfd<<1;
    rdcount++;

    // queue up a 2>&1 ?
    if (strchr(ss, '&')) {
      if (!(31&++rdcount)) rd = xrealloc(rd, (2*rdcount+66)*sizeof(int *));
      rr = rd+3+rdcount;
      rr[0] = 2;
      rr[1] = 1+(1<<1);
      rdcount++;
    }
  }
  if (rd) rd[2] = rdcount;

// todo: ok, now _use_ in_rd[in_rdcount] and rd[rdcount]. :)

// todo: handle ((math)) here

// todo use envlen
// todo: check for functions

  // Is this command a builtin that should run in this process?
  if ((tl = toy_find(*pp->arg.v))
    && (tl->flags & (TOYFLAG_NOFORK|TOYFLAG_MAYFORK)))
  {
    struct toy_context temp;
    sigjmp_buf rebound;

    // This fakes lots of what toybox_main() does.
    memcpy(&temp, &toys, sizeof(struct toy_context));
    memset(&toys, 0, sizeof(struct toy_context));

// todo: redirect stdin/out
    if (!sigsetjmp(rebound, 1)) {
      toys.rebound = &rebound;
// must be null terminated
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
// todo: redirect and pipe
// todo: redirecting stderr needs xpopen3() or rethink
    if (-1 == (pp->pid = xpopen_both(pp->arg.v, pipe))){
      perror_msg("%s: vfork", *pp->arg.v);
// todo: don't close stdin/stdout!
    } else {
	    pp->exit = xpclose_both(pp->pid, 0);
    }
  }

  s = 0;
flush:
  if (s) {
    syntax_err("bad %s", s);
    if (!pp->exit) pp->exit = 1;
  }
  for (j = 0; j<rdcount; j++) if (rd[4+2*j]>6) close(rd[4+2*j]>>1);
  if (rdcount) free(dlist_lpop((void *)rdlist));

  return pp;
}

// parse next word from command line. Returns end, or 0 if need continuation
// caller eats leading spaces
static char *parse_word(char *start)
{
  int i, j, quote = 0, q, qc = 0;
  char *end = start, *s;

  // (( is a special quote at the start of a word
  if (strstart(&end, "((")) toybuf[quote++] = 255;

  // find end of this word
  while (*end) {
    i = 0;

    // barf if we're near overloading quote stack (nesting ridiculously deep)
    if (quote>4000) {
      syntax_err("tilt");
      return (void *)1;
    }

    q = quote ? toybuf[quote-1] : 0;
    // Handle quote contexts
    if (q) {

      // when waiting for parentheses, they nest
      if ((q == ')' || q == '\xff') && (*end == '(' || *end == ')')) {
        if (*end == '(') qc++;
        else if (qc) qc--;
        else if (q == '\xff') {
          // (( can end with )) or retroactively become two (( if we hit one )
          if (strstart(&end, "))")) quote--;
          else return start+1;
        }
        end++;

      // end quote?
      } else if (*end == q) quote--, end++;

      // single quote claims everything
      else if (q == '\'') end++;
      else i++;

      // loop if we already handled a symbol
      if (!i) continue;
    } else {
      // Things that only matter when unquoted

      if (isspace(*end)) break;

      // Things we should only return at the _start_ of a word

      // Redirections. 123<<file- parses as 2 args: "123<<" "file-".
      // Greedy matching: >&; becomes >& ; not > &;
      s = end + redir_prefix(end);
      j = anystart(s, (char *[]){"<<<", "<<-", "<<", "<&", "<>", "<", ">>",
        ">&", ">|", ">", 0});
      if (j) s += j;

      // Control characters
      else s = end + anystart(end, (char *[]){";;&", ";;", ";&", ";", "||",
          "|&", "|", "&&", "&>>", "&>", "&", "(", ")", 0});
      if (s != end) return (end == start) ? s : end;
      i++;
    }

    // Things the same unquoted or in most non-single-quote contexts

    // start new quote context?
    if (strchr("\"'`", *end)) toybuf[quote++] = *end++;
    else if (q != '"' && (strstart(&end, "<(") || strstart(&end,">(")))
      toybuf[quote++]=')';

    // backslash escapes
    else if (*end == '\\') {
      if (!end[1] || (end[1]=='\n' && !end[2])) return 0;
      end += 2;
    } else if (*end++ == '$') {
      if (-1 != (i = stridx("({[", *end))) {
        toybuf[quote++] = ")}]"[i];
        end++;
      }
    }
  }

  return quote ? 0 : end;
}

// if then fi for while until select done done case esac break continue return

// Allocate more space for arg, and possibly terminator
void argxtend(struct sh_arg *arg)
{
  if (!(arg->c&31)) arg->v = xrealloc(arg->v, (33+arg->c)*sizeof(void *));
}

// Pipeline segments
struct sh_pipeline {
  struct sh_pipeline *next, *prev;
  int count, here, type;
  struct sh_arg arg[1];
};

// run a series of "command | command && command" with redirects.
int run_pipeline(struct sh_pipeline **pl, int *rd)
{
  struct sh_process *pp;
  int rc = 0;

  for (;;) {
// todo job control
    if (!(pp = run_command((*pl)->arg, &rd))) rc = 0;
    else {
//wait4(pp);
      llist_traverse(pp->delete, free);
      rc = pp->exit;
      free(pp);
    }

    if ((*pl)->next && !(*pl)->next->type) *pl = (*pl)->next;
    else return rc;
  }
}



// scratch space (state held between calls). Don't want to make it global yet
// because this could be reentrant.
struct sh_function {
  char *name;
  struct sh_pipeline *pipeline;
  struct double_list *expect;
// TODO: lifetime rules for arg? remember "shift" command.
  struct sh_arg *arg; // arguments to function call
  char *end;
};

// Free one pipeline segment.
void free_pipeline(void *pipeline)
{
  struct sh_pipeline *pl = pipeline;
  int i, j;

  if (pl) for (j=0; j<=pl->count; j++) {
    for (i = 0; i<=pl->arg->c; i++)  free(pl->arg[j].v[i]);
    free(pl->arg[j].v);
  }
  free(pl);
}

// Return end of current block, or NULL if we weren't in block and fell off end.
struct sh_pipeline *block_end(struct sh_pipeline *pl)
{
  int i = 0;

  while (pl) {
    if (pl->type == 1 || pl->type == 'f') i++;
    else if (pl->type == 3) if (--i<1) break;
    pl = pl->next;
  }

  return 0;
}

void free_function(struct sh_function *sp)
{
  llist_traverse(sp->pipeline, free_pipeline);
  llist_traverse(sp->expect, free);
  memset(sp, 0, sizeof(struct sh_function));
}

// TODO this has to add to a namespace context. Functions within functions...
struct sh_pipeline *add_function(char *name, struct sh_pipeline *pl)
{
dprintf(2, "stub add_function");

  return block_end(pl->next);
}

// Add a line of shell script to a shell function. Returns 0 if finished,
// 1 to request another line of input (> prompt), -1 for syntax err
static int parse_line(char *line, struct sh_function *sp)
{
  char *start = line, *delete = 0, *end, *last = 0, *s, *ex, done = 0;
  struct sh_pipeline *pl = sp->pipeline ? sp->pipeline->prev : 0;
  struct sh_arg *arg = 0;
  long i;

  // Resume appending to last statement?
  if (pl) {
    arg = pl->arg;

    // Extend/resume quoted block
    if (arg->c<0) {
      delete = start = xmprintf("%s%s", arg->v[arg->c = (-arg->c)-1], start);
      free(arg->v[arg->c]);
      arg->v[arg->c] = 0;

    // is a HERE document in progress?
    } else if (pl->count != pl->here) {
      arg += 1+pl->here;

      argxtend(arg);
      if (strcmp(line, arg->v[arg->c])) {
        // Add this line
        arg->v[arg->c+1] = arg->v[arg->c];
        arg->v[arg->c++] = xstrdup(line);
      // EOF hit, end HERE document
      } else {
        arg->v[arg->c] = 0;
        pl->here++;
      }
      start = 0;

    // Nope, new segment
    } else pl = 0;
  }

  // Parse words, assemble argv[] pipelines, check flow control and HERE docs
  if (start) for (;;) {
    ex = sp->expect ? sp->expect->prev->data : 0;

    // Look for << HERE redirections in completed pipeline segment
    if (pl && pl->count == -1) {
      pl->count = 0;
      arg = pl->arg;

      // find arguments of the form [{n}]<<[-] with another one after it
      for (i = 0; i<arg->c; i++) {
        s = arg->v[i] + redir_prefix(arg->v[i]);
        if (strcmp(s, "<<") && strcmp(s, "<<-") && strcmp(s, "<<<")) continue;
        if (i+1 == arg->c) goto flush;

        // Add another arg[] to the pipeline segment (removing/readding to list
        // because realloc can move pointer)
        dlist_lpop(&sp->pipeline);
        pl = xrealloc(pl, sizeof(*pl) + ++pl->count*sizeof(struct sh_arg));
        dlist_add_nomalloc((void *)&sp->pipeline, (void *)pl);

        // queue up HERE EOF so input loop asks for more lines.
        arg[pl->count].v = xzalloc(2*sizeof(void *));
        *arg[pl->count].v = arg->v[++i];
        arg[pl->count].c = -(s[2] == '<'); // note <<< as c = -1
      }
      pl = 0;
    }
    if (done) break;
    s = 0;

    // skip leading whitespace/comment here to know where next word starts
    for (;;) {
      if (isspace(*start)) ++start;
      else if (*start=='#') while (*start && *start != '\n') ++start;
      else break;
    }

    // Parse next word and detect overflow (too many nested quotes).
    if ((end = parse_word(start)) == (void *)1)
      goto flush;

    // Is this a new pipeline segment?
    if (!pl) {
      pl = xzalloc(sizeof(struct sh_pipeline));
      arg = pl->arg;
      dlist_add_nomalloc((void *)&sp->pipeline, (void *)pl);
    }
    argxtend(arg);

    // Do we need to request another line to finish word (find ending quote)?
    if (!end) {
      // Save unparsed bit of this line, we'll need to re-parse it.
      arg->v[arg->c] = xstrndup(start, strlen(start));
      arg->c = -(arg->c+1);
      free(delete);

      return 1;
    }

    // Ok, we have a word. What does it _mean_?

    // Did we hit end of line or ) outside a function declaration?
    // ) is only saved at start of a statement, ends current statement
    if (end == start || (arg->c && *start == ')' && pl->type!='f')) {
      arg->v[arg->c] = 0;

      if (pl->type == 'f' && arg->c<3) {
        s = "function()";
        goto flush;
      }

      // "for" on its own line is an error.
      if (arg->c == 1 && ex && !memcmp(ex, "do\0A", 4)) {
        s = "newline";
        goto flush;
      }

      // don't save blank pipeline segments
      if (!arg->c) free_pipeline(dlist_lpop(&sp->pipeline));

      // stop at EOL, else continue with new pipeline segment for )
      if (end == start) done++;
      pl->count = -1;
      last = 0;

      continue;
    }

    // Save argument (strdup) and check for flow control
    s = arg->v[arg->c] = xstrndup(start, end-start);
    start = end;
    if (strchr(";|&", *s)) {

      // flow control without a statement is an error
      if (!arg->c) goto flush;

      // treat ; as newline so we don't have to check both elsewhere.
      if (!strcmp(s, ";")) {
        arg->v[arg->c] = 0;
        free(s);
        s = 0;
      }
      last = s;
      pl->count = -1;

      continue;
    } else arg->v[++arg->c] = 0;

    // is a function() in progress?
    if (arg->c>1 && !strcmp(s, "(")) pl->type = 'f';
    if (pl->type=='f') {
      if (arg->c == 2 && strcmp(s, "(")) goto flush;
      if (arg->c == 3) {
        if (strcmp(s, ")")) goto flush;

        // end function segment, expect function body
        pl->count = -1;
        last = 0;
        dlist_add(&sp->expect, "}");
        dlist_add(&sp->expect, 0);
        dlist_add(&sp->expect, "{");

        continue;
      }

    // a for/select must have at least one additional argument on same line
    } else if (ex && !memcmp(ex, "do\0A", 4)) {

      // Sanity check and break the segment
      if (strncmp(s, "((", 2) && strchr(s, '=')) goto flush;
      pl->count = -1;
      sp->expect->prev->data = "do\0C";

      continue;

    // flow control is the first word of a pipeline segment
    } else if (arg->c>1) continue;

    // Do we expect something that _must_ come next? (no multiple statements)
    if (ex) {
      // When waiting for { it must be next symbol, but can be on a new line.
      if (!strcmp(ex, "{")) {
        if (strcmp(s, "{")) goto flush;
        free(arg->v[--arg->c]);  // don't save the {, function starts the block
        free(dlist_lpop(&sp->expect));

        continue;

      // The "test" part of for/select loops can have (at most) one "in" line,
      // for {((;;))|name [in...]} do
      } else if (!memcmp(ex, "do\0C", 4)) {
        if (strcmp(s, "do")) {
          // can only have one "in" line between for/do, but not with for(())
          if (!pl->prev->type) goto flush;
          if (!strncmp(pl->prev->arg->v[1], "((", 2)) goto flush;
          else if (!strcmp(s, "in")) goto flush;

          continue;
        }
      }
    }

    // start of a new block?

    // for/select requires variable name on same line, can't break segment yet
    if (!strcmp(s, "for") || !strcmp(s, "select")) {
      if (!pl->type) pl->type = 1;
      dlist_add(&sp->expect, "do\0A");

      continue;
    }

    end = 0;
    if (!strcmp(s, "if")) end = "then";
    else if (!strcmp(s, "while") || !strcmp(s, "until")) end = "do\0B";
    else if (!strcmp(s, "case")) end = "esac";
    else if (!strcmp(s, "{")) end = "}";
    else if (!strcmp(s, "[[")) end = "]]";
    else if (!strcmp(s, "(")) end = ")";

    // Expecting NULL means a statement: I.E. any otherwise unrecognized word
    else if (sp->expect && !ex) {
      free(dlist_lpop(&sp->expect));
      continue;
    } else if (!ex) goto check;

    // Did we start a new statement?
    if (end) {
      pl->type = 1;

      // Only innermost statement needed in { { { echo ;} ;} ;} and such
      if (sp->expect && !sp->expect->prev->data) free(dlist_lpop(&sp->expect));

    // If we got here we expect a specific word to end this block: is this it?
    } else if (!strcmp(s, ex)) {
      // can't "if | then" or "while && do", only ; & or newline works
      if (last && (strcmp(ex, "then") || strcmp(last, "&"))) {
        s = end;
        goto flush;
      }

      free(dlist_lpop(&sp->expect));
      pl->type = anystr(s, (char *[]){"fi", "done", "esac", "}", "]]", ")", 0})
        ? 3 : 2;

      // if it's a multipart block, what comes next?
      if (!strcmp(s, "do")) end = "done";
      else if (!strcmp(s, "then")) end = "fi\0A";

    // fi could have elif, which queues a then.
    } else if (!strcmp(ex, "fi")) {
      if (!strcmp(s, "elif")) {
        free(dlist_lpop(&sp->expect));
        end = "then";
      // catch duplicate else while we're here
      } else if (!strcmp(s, "else")) {
        if (ex[3] != 'A') {
          s = "2 else";
          goto flush;
        }
        free(dlist_lpop(&sp->expect));
        end = "fi\0B";
      }
    }

    // Do we need to queue up the next thing to expect?
    if (end) {
      if (!pl->type) pl->type = 2;
      dlist_add(&sp->expect, end);
      dlist_add(&sp->expect, 0);    // they're all preceded by a statement
      pl->count = -1;
    }

check:
    // syntax error check: these can't be the first word in an unexpected place
    if (!pl->type && anystr(s, (char *[]){"then", "do", "esac", "}", "]]", ")",
        "done", "fi", "elif", "else", 0})) goto flush;
  }
  free(delete);

  // advance past <<< arguments (stored as here documents, but no new input)
  pl = sp->pipeline->prev;
  while (pl->count<pl->here && pl->arg[pl->count].c<0)
    pl->arg[pl->count++].c = 0;

  // return if HERE document pending or more flow control needed to complete
  if (sp->expect) return 1;
  if (sp->pipeline && pl->count != pl->here) return 1;
  dlist_terminate(sp->pipeline);

  // Don't need more input, can start executing.

  return 0;

flush:
  if (s) syntax_err("bad %s", s);
  free_function(sp);

  return 0-!!s;
}

static void dump_state(struct sh_function *sp)
{
  struct sh_pipeline *pl;
  int q = 0;
  long i;

  if (sp->expect) {
    struct double_list *dl;

    for (dl = sp->expect; dl; dl = (dl->next == sp->expect) ? 0 : dl->next)
      dprintf(2, "expecting %s\n", dl->data);
    if (sp->pipeline)
      dprintf(2, "pipeline count=%d here=%d\n", sp->pipeline->prev->count,
        sp->pipeline->prev->here);
  }

  for (pl = sp->pipeline; pl ; pl = (pl->next == sp->pipeline) ? 0 : pl->next) {
    for (i = 0; i<pl->arg->c; i++)
      printf("arg[%d][%ld]=%s\n", q, i, pl->arg->v[i]);
    printf("type=%d term[%d]=%s\n", pl->type, q++, pl->arg->v[pl->arg->c]);
  }
}

// run a shell function, handling flow control statements
static void run_function(struct sh_function *sp)
{
  struct sh_pipeline *pl = sp->pipeline, *end;
  struct blockstack {
    struct blockstack *next;
    struct sh_pipeline *start, *end;
    int run, loop, *redir;

    struct sh_arg farg;          // for/select arg stack
    struct string_list *fdelete; // farg's cleanup list
    char *fvar;                  // for/select's iteration variable name
  } *blk = 0, *new;
  long i;

  // iterate through the commands
  while (pl) {
    char *s = *pl->arg->v, *ss = pl->arg->v[1];

    // Normal executable statement?
    if (!pl->type) {
// TODO: break & is supported? Seriously? Also break > potato
      if (!strcmp(s, "break") || !strcmp(s, "continue")) {
        i = ss ? atol(ss) : 0;
        if (i<1) i = 1;
        if (!blk || pl->arg->c>2 || ss[strspn(ss, "0123456789")]) {
          syntax_err("bad %s", s);
          break;
        }
        i = atol(ss);
        if (!i) i++;
        while (i && blk) {
          if (--i && *s == 'c') {
            pl = blk->start;
            break;
          }
          pl = blk->end;
          llist_traverse(blk->fdelete, free);
          free(llist_pop(&blk));
        }
        pl = pl->next;

        continue;
      }

// inherit redirects?
// returns last statement of pipeline
      if (!blk) toys.exitval = run_pipeline(&pl, 0);
      else if (blk->run) toys.exitval = run_pipeline(&pl, blk->redir);
      else do pl = pl->next; while (!pl->type);

    // Starting a new block?
    } else if (pl->type == 1) {

/*
if/then/elif/else/fi
for select while until/do/done
case/esac
{/}
[[/]]
(/)
((/))
function/}
*/

      // Is this new, or did we just loop?
      if (!blk || blk->start != pl) {

        // If it's a nested block we're not running, skip ahead.
        end = block_end(pl->next);
        if (blk && !blk->run) {
          pl = end;
          if (pl) pl = pl->next;
          continue;
        }

        // If new block we're running, save context and add it to the stack.
        new = xzalloc(sizeof(*blk));
        new->next = blk;
        blk = new;
        blk->start = pl;
        blk->end = end;
        blk->run = 1;
// todo perform block end redirects to blk->redir
      }

      // What flow control statement is this?
      if (!strcmp(s, "for") || !strcmp(s, "select")) {
        if (!strncmp(blk->fvar = ss, "((", 2)) {
dprintf(2, "skipped init for((;;)), need math parser\n");
        } else {
          // populate blk->farg with expanded arguments
          if (pl->next->type) {
            for (i = 1; i<pl->next->arg->c; i++)
              expand_arg(&blk->farg, pl->next->arg->v[i], 0, &blk->fdelete);
            pl = pl->next;
          } else expand_arg(&blk->farg, "\"$@\"", 0, &blk->fdelete);
        }
      } 

    // gearshift from block start to block body
    } else if (pl->type == 2) {

      // Handle if statement
      if (!strcmp(s, "then")) blk->run = blk->run && !toys.exitval;
      else if (!strcmp(s, "else") || !strcmp(s, "elif")) blk->run = !blk->run;
      else if (!strcmp(s, "do")) {
        if (!strcmp(*blk->start->arg->v, "while"))
          blk->run = blk->run && !toys.exitval;
        else if (blk->loop >= blk->farg.c) {
          blk->run = 0;
          pl = block_end(pl);
          continue;
        } else if (!strncmp(blk->fvar, "((", 2)) {
dprintf(2, "skipped running for((;;)), need math parser\n");
        } else setvar(xmprintf("%s=%s", blk->fvar, blk->farg.v[blk->loop]),
          TAKE_MEM);
      }

    // end of block
    } else if (pl->type == 3) {

      // repeating block?
      if (blk->run && !strcmp(s, "done")) {
        pl = blk->start;
        continue;
      }

      // if ending a block, pop stack.
      llist_traverse(blk->fdelete, free);
      free(llist_pop(&blk));

// todo unwind redirects (cleanup blk->redir)

    } else if (pl->type == 'f') pl = add_function(s, pl);

    pl = pl->next;
  }

  // Cleanup from syntax_err();
  while (blk) {
    llist_traverse(blk->fdelete, free);
    free(llist_pop(&blk));
  }

  return;
}

void subshell_imports(void)
{
/*
  // TODO cull local variables because 'env "()=42" env | grep 42' works.

  // vfork() means subshells have to export and then re-import locals/functions
  sprintf(toybuf, "(%d#%d)", getpid(), getppid());
  if ((s = getenv(toybuf))) {
    char *from, *to, *ss;

    unsetenv(toybuf);
    ss = s;

    // Loop through packing \\ until \0
    for (from = to = s; *from; from++, to++) {
      *to = *from;
      if (*from != '\\') continue;
      if (from[1] == '\\' || from[1] == '0') from++;
      if (from[1] != '0') continue;
      *to = 0;

      // save chunk
      for (ss = s; ss<to; ss++) {
        if (*ss == '=') {
          // first char of name is variable type ala declare
          if (s+1<ss && strchr("aAilnru", *s)) {
            setvar(ss, *s);

            break;
          }
        } else if (!strncmp(ss, "(){", 3)) {
          FILE *ff = fmemopen(s, to-s, "r");

          while ((new = xgetline(ff, 0))) {
            if ((prompt = parse_line(new, &scratch))<0) break;
            free(new);
          }
          if (!prompt) {
            add_function(s, scratch.pipeline);
            free_function(&scratch);
            break;
          }
          fclose(ff);
        } else if (!isspace(*s) && !ispunct(*s)) continue;

        error_exit("bad locals");
      }
      s = from+1;
    }
  }
*/
}

int sh_run( char* line_iterator( void * iterator_data, int prev_consumed ), void * iterator_data )
{
  struct sh_function scratch;
  int prev_consumed = 0;
  static int first_time = 1;

  // Read environment for exports from parent shell
  if (first_time) {
    subshell_imports();
    first_time = 0;
  }
  
  memset(&scratch, 0, sizeof(scratch));

  for (;;) {

    char * new = line_iterator( iterator_data, prev_consumed );
    if (new == NULL) break;

// TODO if (!isspace(*new)) add_to_history(line);

    // returns 0 if line consumed, command if it needs more data
    prev_consumed = parse_line(new, &scratch);
//dump_state(&scratch);
    if (prev_consumed != 1) {
// TODO: ./blah.sh one two three: put one two three in scratch.arg
      if (!prev_consumed) run_function(&scratch);
      free_function(&scratch);
      prev_consumed = 0;
    }
    free(new);
  }

  return prev_consumed;
}

char* file_line_iterator( void * iterator_data, int prev_consumed ) {
  
    FILE * f = (FILE *) iterator_data;
    // Prompt and read line
    if (f == stdin) {
      char *s = getenv(prev_consumed ? "PS2" : "PS1");

      if (!s) s = prev_consumed ? "> " : (getpid() ? "\\$ " : "# ");
      do_prompt(s);
    } else TT.lineno++;
    return xgetline(f ? f : stdin, 0);
}

void sh_main(void)
{
  FILE * f;
  int prompt;
  
  if (TT.command) f = fmemopen(TT.command, strlen(TT.command), "r");
  else if (*toys.optargs) f = xfopen(*toys.optargs, "r");
  else {
    f = stdin;
    if (isatty(0)) toys.optflags |= FLAG_i;
  }

  prompt = sh_run( file_line_iterator, f );

  if (prompt) error_exit("%ld:unfinished line"+4*!TT.lineno, TT.lineno);
  toys.exitval = f && ferror(f);
}

