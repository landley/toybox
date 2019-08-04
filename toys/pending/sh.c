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
static void expand_arg(struct sh_arg *arg, char *new, unsigned flags)
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
static int anyof(char *s, char **try)
{
  while (*try) {
    if (strstart(&s, *try)) return strlen(*try);
    try++;
  }

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
    for (j=0; s[j] && s[j] != '=' && s[j] != '\\'; j++);
    if (s[j] != '=') break;
  }

  // perform assignments locally if there's no command
  if (envlen == arg->c) {
    for (j = 0; j<envlen; j++) {
      struct sh_arg aa;

      aa.c = 0;
      expand_arg(&aa, arg->v[j], NO_PATH|NO_SPLIT);
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
    fd = anyof(ss, (char *[]){"<<<", "<<-", "<<", "<&", "<>", "<", ">>", ">&",
      ">|", ">", "&>>", "&>", 0});
    if (!fd) {

      // Nope: save/expand argument and loop
      expand_arg(&pp->arg, s, 0);

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
      expand_arg(&pp->arg, arg->v[j], NO_PATH|(NO_SPLIT*!strcmp(ss, "<<<")));
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
    if (-1 == (pp->pid = xpopen_both(pp->arg.v, pipe)))
      perror_msg("%s: vfork", *pp->arg.v);
// todo: don't close stdin/stdout!
    else pp->exit = xpclose_both(pp->pid, 0);
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
  int i, j, quote = 0;
  char *end = start, *s;

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
        s = end + redir_prefix(end);
        j = anyof(s, (char *[]){"<<<", "<<-", "<<", "<&", "<>", "<", ">>",
          ">&", ">|", ">", 0});
        if (j) s += j;
        else s = end + anyof(s, (char *[]){";;&", ";;", ";&", ";", "||", "|&",
            "|", "&&", "&>>", "&>", "&", "(", ")", 0});
        if (s != end) return (end == start) ? s : end;
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
    pp = run_command((*pl)->arg, &rd);
//wait4(pp);
    llist_traverse(pp->delete, free);
    rc = pp->exit;
    free(pp);

    if ((*pl)->next && !(*pl)->next->type) *pl = (*pl)->next;
    else return rc;
  }
}



// scratch space (state held between calls). Don't want to make it global yet
// because this could be reentrant.
struct sh_parse {
  struct sh_pipeline *pipeline;
  struct double_list *expect;
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

// Return end of block corresponding to start of block
struct sh_pipeline *block_end(struct sh_pipeline *pl)
{
  int i = 0;
  do {
    if (pl->type == 1 || pl->type == 'f') i++;
    else if (pl->type == 3) i--;
    pl = pl->next;
  } while (i);

  return pl;
}

// Consume a line of shell script and do what it says. Returns 0 if finished,
// 1 to request another line of input (> prompt).
static int parse_line(char *line, struct sh_parse *sp)
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
    }
  }

  // Parse words, assemble argv[] pipelines, check flow control and HERE docs
  if (start) for (;;) {

    // Grab HERE document(s) from completed pipeline segment
    if (pl && pl->count == -1) {
      pl->count = 0;
      arg = pl->arg;

      // find arguments of the form [{n}]<<[-] with another one after it
      for (i = 0; i<arg->c; i++) {
        s = arg->v[i] + redir_prefix(arg->v[i]);
        if (strcmp(s, "<<") && strcmp(s, "<<-") && strcmp(s, "<<<")) continue;
        if (i+1 == arg->c) goto flush;

        // queue up HERE EOF so input loop asks for more lines.
        dlist_lpop(&sp->pipeline);
        pl = xrealloc(pl, sizeof(*pl) + ++pl->count*sizeof(struct sh_arg));
        dlist_add_nomalloc((void *)&sp->pipeline, (void *)pl);

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
    if ((end = parse_word(start)) == (void *)1) goto flush;

    // Extend pipeline and argv[] to store result
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

    // flow control is the first word of a pipeline segment
    } else if (arg->c>1) continue;
    ex = sp->expect ? sp->expect->prev->data : 0;

    // When waiting for { it must be next symbol, but can be on a new line.
    if (ex && !strcmp(ex, "{")) {
      if (strcmp(s, "{") || *end) goto flush;
      free(arg->v[--arg->c]);
      free(dlist_lpop(&sp->expect));

      continue;
    }

    end = 0;
    if (!strcmp(s, "if")) end = "then";
    else if (!strcmp(s, "for") || !strcmp(s, "select")
         || !strcmp(s, "while") || !strcmp(s, "until")) end = "do";
    else if (!strcmp(s, "case")) end = "esac";
    else if (!strcmp(s, "{")) end = "}";
    else if (!strcmp(s, "[[")) end = "]]";
    else if (!strcmp(s, "(")) end = ")";

    // Expecting NULL means a statement: any otherwise unrecognized word
    else if (sp->expect && !ex) {
      free(dlist_lpop(&sp->expect));
      continue;
    } else if (!ex) goto check;

    if (end) {
      pl->type = 1;

      // Only innermost statement needed in { { { echo ;} ;} ;} and such
      if (sp->expect && !sp->expect->prev->data) free(dlist_lpop(&sp->expect));

    // If we got here we expect a specific word to end this block: is this it?
    } else if (!strcmp(s, ex)) {
      // can't "if | then" or "while && do", only ; & or newline works
      if (last && strcmp(last, "&")) {
        s = end;
        goto flush;
      }

      free(dlist_lpop(&sp->expect));
      pl->type = anyof(s, (char *[]){"fi", "done", "esac", "}", "]]", ")"})
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
    if (!pl->type && anyof(s, (char *[]){"then", "do", "esac", "}", "]]", ")",
        "done", "then", "fi", "elif", "else"})) goto flush;
  }


  free(delete);

if (0) if (sp->expect) {
dprintf(2, "expectorate\n");
struct double_list *dl;
for (dl = sp->expect; dl; dl = (dl->next == sp->expect) ? 0 : dl->next)
  dprintf(2, "expecting %s\n", dl->data);
if (sp->pipeline) dprintf(2, "count=%d here=%d\n", sp->pipeline->prev->count, sp->pipeline->prev->here);
}

  // advance past <<< arguments (stored as here documents, but no new input)
  pl = sp->pipeline->prev;
  while (pl->count<pl->here && pl->arg[pl->count].c<0)
    pl->arg[pl->count++].c = 0;

  // return if HERE document pending or more flow control needed to complete
  if (sp->expect) return 1;
  if (sp->pipeline && pl->count != pl->here) return 1;

  // At this point, we've don't need more input and can start executing.

if (0) {
dprintf(2, "pipeline now\n");
struct sh_pipeline *ppl = pl;
int q = 0;
for (pl = sp->pipeline; pl ; pl = (pl->next == sp->pipeline) ? 0 : pl->next) {
  for (i = 0; i<pl->arg->c; i++) printf("arg[%d][%ld]=%s\n", q, i, pl->arg->v[i]);
  printf("type=%d term[%d]=%s\n", pl->type, q++, pl->arg->v[pl->arg->c]);
}
pl = ppl;
}

  // **************************** do the thing *******************************

  // Now we have a complete thought and can start running stuff.

  struct blockstack {
    struct blockstack *next;
    struct sh_pipeline *start, *now, *end;
    int run, val, *redir;
  } *blk = 0, *new;

  // iterate through the commands
  dlist_terminate(pl = sp->pipeline);
  while (pl) {
    s = *pl->arg->v;

    // Normal executable statement?
    if (!pl->type) {

// inherit redirects?
// returns last statement of pipeline
      if (!blk) toys.exitval = run_pipeline(&pl, 0);
      else if (blk->run) toys.exitval = run_pipeline(&pl, blk->redir);
      else do pl = pl->next; while (!pl->type);

    // Starting a new block?
    } else if (pl->type == 1) {
      struct sh_pipeline *end = block_end(pl);

      // If we're not running this, skip ahead.
      if (blk && !blk->run) {
        pl = end;
        continue;
      }

      // If we are, save context and add it to the stack.
      new = xzalloc(sizeof(*blk));
      new->next = blk;
      blk = new;
      blk->start = blk->now = pl;
      blk->end = end;
      blk->run = 1;

// todo perform block end redirects

    } else if (pl->type == 2) {

      // Handle if statement
      if (!strcmp(s, "then")) blk->run = blk->run && !toys.exitval;
      else if (!strcmp(s, "else") || !strcmp(s, "elif")) blk->run = !blk->run;

    // If ending a block, pop stack.
    } else if (pl->type == 3) {
      new = blk->next;
      free(blk);
      blk = new;

// todo unwind redirects

    } else if (pl->type == 'f') {
dprintf(2, "skipped function definition %s\n", *pl->arg->v);
pl = block_end(pl);
    }



/*
if/then/elif/else/fi
for select while until/do/done
case/esac
{/}
[[/]]
(/)
function/}
*/
    pl = pl->next;
  }

  s = 0;
flush:
  if (s) syntax_err("bad %s", s);
  while ((pl = dlist_pop(&sp->pipeline))) free_pipeline(pl);
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

// TODO if (!isspace(*new)) add_to_history(line);

    // returns 0 if line consumed, command if it needs more data
    prompt = parse_line(new, &scratch);
    free(new);
  }

  if (prompt) error_exit("%ld:unfinished line"+4*!TT.lineno, TT.lineno);
  toys.exitval = f && ferror(f);
}
