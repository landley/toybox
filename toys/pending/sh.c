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
 * existing but considered builtins: false kill pwd true time
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

USE_SH(NEWTOY(sh, "(noediting)(noprofile)(norc)sc:i", TOYFLAG_BIN))
USE_SH(OLDTOY(toysh, sh, TOYFLAG_BIN))
USE_SH(OLDTOY(bash, sh, TOYFLAG_BIN))
// Login lies in argv[0], so add some aliases to catch that
USE_SH(OLDTOY(-sh, sh, 0))
USE_SH(OLDTOY(-toysh, sh, 0))
USE_SH(OLDTOY(-bash, sh, 0))

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

  char **locals;

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
      struct sh_process *next, *prev;
      struct string_list *delete;   // expanded strings
      int *urd, pid, exit;          // undo redirects, child PID, exit status
      struct sh_arg arg;
    } *procs, *proc;
  } *jobs, *job;
  unsigned jobcnt;
  int hfd, *hfdclose;  // next high filehandle (>= 10)
)


// ordered for greedy matching, so >&; becomes >& ; not > &;
// making these const means I need to typecast the const away later to
// avoid endless warnings.
static const char *redirectors[] = {"<<<", "<<-", "<<", "<&", "<>", "<", ">>",
  ">&", ">|", ">", "&>>", "&>", 0};

#define SH_NOCLOBBER 1   // set -C

void cd_main(void)
{
  char *dest = *toys.optargs ? *toys.optargs : getenv("HOME");

// TODO: -LPE@
// TODO: cd .. goes up $PWD path we used to get here, not ./..
  xchdir(dest ? dest : "/");
}

void exit_main(void)
{
  exit(*toys.optargs ? atoi(*toys.optargs) : 0);
}

// like error_msg() but exit from shell scripts
static void syntax_err(char *msg, ...)
{
  va_list va;

  va_start(va, msg);
  verror_msg(msg, 0, va);
  va_end(va);

  if (*toys.optargs) xexit();
}

// Print prompt to stderr, parsing escapes
// Truncated to 4k at the moment, waiting for somebody to complain.
static void do_prompt(char *prompt)
{
  char *s, c, cc, *pp = toybuf;
  int len;

  if (!prompt) prompt = "\\$ ";
  while ((len = sizeof(toybuf)-(pp-toybuf))>0 && *prompt) {
    c = *(prompt++);

    if (c=='!') {
      if (*prompt=='!') prompt++;
      else {
        pp += snprintf(pp, len, "%ld", TT.lineno);
        continue;
      }
    } else if (c=='\\') {
      cc = *(prompt++);
      if (!cc) {
        *pp++ = c;
        break;
      }

      // \nnn \dD{}hHjlstT@AuvVwW!#$
      // Ignore bash's "nonprintable" hack; query our cursor position instead.
      if (cc=='[' || cc==']') continue;
      else if (cc=='$') *pp++ = getuid() ? '$' : '#';
      else if (cc=='h' || cc=='H') {
        *pp = 0;
        gethostname(pp, len);
        pp[len-1] = 0;
        if (cc=='h' && (s = strchr(pp, '.'))) *s = 0;
        pp += strlen(pp);
      } else if (cc=='s') {
        s = getbasename(*toys.argv);
        while (*s && len--) *pp++ = *s++;
      } else if (!(c = unescape(cc))) {
        *pp++ = '\\';
        if (--len) *pp++ = c;
      } else *pp++ = c;
    } else *pp++ = c;
  }
  len = pp-toybuf;
  if (len>=sizeof(toybuf)) len = sizeof(toybuf);
  writeall(2, toybuf, len);
}

// quote removal, brace, tilde, parameter/variable, $(command),
// $((arithmetic)), split, path 
#define NO_PATH  (1<<0)
#define NO_SPLIT (1<<1)
#define NO_BRACE (1<<2)
#define NO_TILDE (1<<3)
#define NO_QUOTE (1<<4)
// TODO: ${name:?error} causes an error/abort here (syntax_err longjmp?)
// TODO: $1 $@ $* need args marshalled down here: function+structure?
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

// Expand exactly one arg, returning NULL if it split.
// If return != new you need to free it.
static char *expand_one_arg(char *new, unsigned flags)
{
  struct sh_arg arg;
  char *s = 0;
  int i;

  memset(&arg, 0, sizeof(arg));
  expand_arg(&arg, new, flags, 0);
  if (arg.c == 1) s = *arg.v;
  else for (i = 0; i < arg.c; i++) free(arg.v[i]);
  free(arg.v);

  return s;
}

// Assign one variable
// s: key=val
// type: 0 = whatever it was before, local otherwise
#define TAKE_MEM 0x80000000
// declare -aAilnrux
// ft
static void setvar(char *s, unsigned type)
{
  if (type&TAKE_MEM) type ^= TAKE_MEM;
  else s = xstrdup(s);

  // local, export, readonly, integer...
  xsetenv(s, 0);
}

// get variable of length len starting at s.
static char *getvar(char *s, int len)
{
  unsigned uu;
  char **ss = TT.locals;

  // loop through local, then global variables
  for (uu = 0; ; uu++) {
    if (!ss[uu]) {
      if (ss != TT.locals) return 0;
      ss = environ;
      uu = 0;
    }
    // Use UHF rubik's cube protocol to find match.
    if (!strncmp(ss[uu], s, len) && ss[uu][len] == '=') return ss[uu]+len+1;
  }
}

// return length of match found at this point
static int anystart(char *s, char **try)
{
  char *ss = s;

  while (*try) {
    if (strstart(&s, *try)) return s-ss;
    try++;
  }

  return 0;
}

// is this one of the strings in try[] (null terminated array)
static int anystr(char *s, char **try)
{
  while (*try) if (!strcmp(s, *try++)) return 1;

  return 0;
}

// return length of valid prefix that could go before redirect
static int redir_prefix(char *word)
{
  char *s = word;

  if (*s == '{') {
    for (s++; isalnum(*s) || *s=='_'; s++);
    if (*s == '}' && s != word+1) s++;
    else s = word;
  } else while (isdigit(*s)) s++;

  return s-word;
}

// TODO |&

// Return number of entries at the start that are environment variable
// assignments, and perform assignments if nothing else on the line
static int assign_env(struct sh_arg *arg)
{
  int envlen, j;
  char *s;

  // Grab variable assignments
  for (envlen = 0; envlen<arg->c; envlen++) {
    s = arg->v[envlen];
    for (j=0; s[j] && (s[j]=='_' || !ispunct(s[j])); j++);
    if (!j || s[j] != '=') break;
  }

  // perform assignments locally if there's no command
  if (envlen != arg->c) return envlen;

  for (j = 0; j<envlen; j++) {
    s = expand_one_arg(arg->v[j], NO_PATH|NO_SPLIT);
    setvar(s, TAKE_MEM*(s!=arg->v[j]));
  }

  return 0;
}

// restore displaced filehandles, closing high filehandles they were copied to
static void unredirect(int *urd)
{
  int *rr = urd+1, i;

  if (!urd) return;

  for (i = 0; i<*urd; i++) {
    if (rr[1] != -1) {
      // No idea what to do about fd exhaustion here, so Steinbach's Guideline.
      dup2(rr[0], rr[1]);
      close(rr[0]);
    }
    rr += 2;
  }
  free(urd);
}

// Return next available high (>=10) file descriptor
int next_hfd()
{
  int hfd;

  for (; TT.hfd<=99999; TT.hfd++) if (-1 == fcntl(TT.hfd, F_GETFL)) break;
  hfd = TT.hfd;
  if (TT.hfd > 99999) {
    hfd = -1;
    if (!errno) errno = EMFILE;
  }

  return hfd;
}

// Perform a redirect, saving displaced filehandle to a high (>10) fd
// rd is an int array: [0] = count, followed by from/to pairs to restore later.
// If from == -1 just save to, else dup from->to after saving to.
int save_redirect(int **rd, int from, int to)
{
  int cnt, hfd, *rr;

  // save displaced to, copying to high (>=10) file descriptor to undo later
  // except if we're saving to environment variable instead (don't undo that)
  if ((hfd = next_hfd())==-1 || hfd != dup2(to, hfd)) return 1;
  fcntl(hfd, F_SETFD, FD_CLOEXEC);

  // dup "to"
  if (from != -1 && to != dup2(from, to)) {
    close(hfd);

    return 1;
  }

  // Append undo information to redirect list so we can restore saved hfd later.
  if (!((cnt = *rd ? **rd : 0)&31)) *rd = xrealloc(*rd, (cnt+33)*2*sizeof(int));
  *(rr = *rd) = ++cnt;
  rr[2*cnt-1] = hfd;
  rr[2*cnt] = to;

  return 0;
}

// Expand arguments and perform redirections. Return new process object with
// expanded args. This can be called from command or block context.
static struct sh_process *expand_redir(struct sh_arg *arg, int envlen, int *urd)
{
  struct sh_process *pp;
  char *s, *ss, *sss, *cv = 0;
  int j, to, from, here = 0;

  TT.hfd = 10;

  if (envlen<0 || envlen>=arg->c) return 0;
  pp = xzalloc(sizeof(struct sh_process));
  pp->urd = urd;

  // When we redirect, we copy each displaced filehandle to restore it later.

  // Expand arguments and perform redirections
  for (j = envlen; j<arg->c; j++) {
    int saveclose = 0;

    // Is this a redirect? s = prefix, ss = operator
    sss = ss = (s = arg->v[j]) + redir_prefix(arg->v[j]);
    sss += anystart(ss, (void *)redirectors);
    if (ss == sss) {
      // Nope: save/expand argument and loop
      expand_arg(&pp->arg, s, 0, &pp->delete);

      continue;
    } else if (j+1 >= arg->c) {
      // redirect needs one argument
      s = "\\n";
      break;
    }
    sss = arg->v[++j];

    // It's a redirect: for [to]<from s = start of [to], ss = <, sss = from

    if (isdigit(*s) && ss-s>5) break;

    // expand arguments for everything but << and <<-
    if (strncmp(ss, "<<", 2) && ss[2] != '<') {
      sss = expand_one_arg(sss, NO_PATH);
      if (!sss) {
        s = sss;
        break; // arg splitting here is an error
      }
      if (sss != arg->v[j]) dlist_add((void *)&pp->delete, sss);
    }

    // Parse the [fd] part of [fd]<name
    to = *ss != '<';
    if (isdigit(*s)) to = atoi(s);
    else if (*s == '{') {
      // when we close a filehandle, we _read_ from {var}, not write to it
      if ((!strcmp(ss, "<&") || !strcmp(ss, ">&")) && !strcmp(sss, "-")) {
        if (!(ss = getvar(s+1, ss-s-2))) break;
        to = atoi(ss); // TODO trailing garbage?
        if (save_redirect(&pp->urd, -1, to)) break;
        close(to);

        continue;
      // record high file descriptor in {to}<from environment variable
      } else {
        // we don't save this, it goes in the env var and user can close it.
        if (-1 == (to = next_hfd())) break;
        cv = xmprintf("%.*s=%d", (int)(ss-s-1), s+1, to);
      }
    }

    // HERE documents?
    if (!strcmp(ss, "<<<") || !strcmp(ss, "<<-") || !strcmp(ss, "<<")) {
      char *tmp = getvar("TMPDIR", 6);
      int i, len, bad = 0, zap = (ss[2] == '-'), x = !ss[strcspn(ss, "\"'")];

      // store contents in open-but-deleted /tmp file.
      tmp = xmprintf("%s/sh-XXXXXX", tmp ? tmp : "/tmp");
      if ((from = mkstemp(tmp))>=0) {
        if (unlink(tmp)) bad++;

        // write contents to file (if <<< else <<) then lseek back to start
        else if (ss[2] == '<') {
          if (x) sss = expand_one_arg(sss, NO_PATH|NO_SPLIT);
          len = strlen(sss);
          if (len != writeall(from, sss, len)) bad++;
          if (x) free(sss);
        } else {
          struct sh_arg *hh = arg+here++;

          for (i = 0; i<hh->c; i++) {
            ss = hh->v[i];
            sss = 0;
            // expand_parameter, commands, and arithmetic
            if (x) ss = sss = expand_one_arg(ss,
              NO_PATH|NO_SPLIT|NO_BRACE|NO_TILDE|NO_QUOTE);

            while (zap && *ss == '\t') ss++;
            x = writeall(from, ss, len = strlen(ss));
            free(sss);
            if (len != x) break;
          }
          if (i != hh->c) bad++;
        }
        if (!bad && lseek(from, 0, SEEK_SET)) bad++;
        if (bad) close(from);
      } else bad++;
      free(tmp);
      if (bad) break;

    // from>=0 means it's fd<<2 (new fd to dup2() after vfork()) plus
    // 2 if we should close(from>>2) after dup2(from>>2, to),
    // 1 if we should close but dup for nofork recovery (ala <&2-)

    // Handle file descriptor duplication/close (&> &>> <& >& with number or -)
    // These redirect existing fd so nothing to open()
    } else if (strchr(ss, '&')) {

      // is there an explicit fd?
      for (ss = sss; isdigit(*ss); ss++);
      if (ss-sss>5 || (*ss && (*ss != '-' || ss[1]))) {
        // bad fd
        s = sss;
        break;
      }

      from = (ss==sss) ? to : atoi(sss);
      if (*ss == '-') saveclose++;
    } else {

      // Permissions to open external file with: < > >> <& >& <> >| &>> &>
      if (!strcmp(ss, "<>")) from = O_CREAT|O_RDWR;
      else if (strstr(ss, ">>")) from = O_CREAT|O_APPEND;
      else {
        from = (*ss != '<') ? O_CREAT|O_WRONLY|O_TRUNC : O_RDONLY;
        if (!strcmp(ss, ">") && (TT.options&SH_NOCLOBBER)) {
          struct stat st;

          // Not _just_ O_EXCL: > /dev/null allowed
          if (stat(sss, &st) || !S_ISREG(st.st_mode)) from |= O_EXCL;
        }
      }

// TODO: /dev/fd/# /dev/{stdin,stdout,stderr} /dev/{tcp,udp}/host/port

// TODO: is umask respected here?
      // Open the file
      if (-1 == (from = xcreate(sss, from|WARN_ONLY, 0666))) break;
    }

    // perform redirect, saving displaced "to".
    save_redirect(&pp->urd, from, to);
    // Do we save displaced "to" in env variable instead of undo list?
    if (cv) {
      --*pp->urd;
      setvar(cv, TAKE_MEM);
      cv = 0;
    }
    if (saveclose) save_redirect(&pp->urd, -1, from);
    close(from);
  }

  if (j != arg->c) {
    syntax_err("bad %s", s);
    if (!pp->exit) pp->exit = 1;
    free(cv);
  }

  return pp;
}

// Execute a single command
static struct sh_process *run_command(struct sh_arg *arg)
{
  struct sh_process *pp;
  struct toy_list *tl;

  // grab environment var assignments, expand arguments and perform redirects
  if (!(pp = expand_redir(arg, assign_env(arg), 0))) return 0;

  // Do nothing if nothing to do
  if (pp->exit || !pp->arg.v);
  else if (!strcmp(*pp->arg.v, "((")) {
    printf("Math!\n");
// TODO: handle ((math))
// TODO: check for functions()
  // Is this command a builtin that should run in this process?
  } else if ((tl = toy_find(*pp->arg.v))
    && (tl->flags & (TOYFLAG_NOFORK|TOYFLAG_MAYFORK)))
  {
    struct toy_context temp;
    sigjmp_buf rebound;

    // This fakes lots of what toybox_main() does.
    memcpy(&temp, &toys, sizeof(struct toy_context));
    memset(&toys, 0, sizeof(struct toy_context));

    if (!sigsetjmp(rebound, 1)) {
      toys.rebound = &rebound;
      toy_init(tl, pp->arg.v);  // arg.v must be null terminated
      tl->toy_main();
    }
    pp->exit = toys.exitval;
    if (toys.optargs != toys.argv+1) free(toys.optargs);
    if (toys.old_umask) umask(toys.old_umask);
    memcpy(&toys, &temp, sizeof(struct toy_context));
  } else {
    if (-1 == (pp->pid = xpopen_both(pp->arg.v, 0)))
      perror_msg("%s: vfork", *pp->arg.v);
  }

  // cleanup process
  unredirect(pp->urd);

  return pp;
}

static void free_process(void *ppp)
{
  struct sh_process *pp = ppp;

  llist_traverse(pp->delete, free);
  free(pp);
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
      s = end + redir_prefix(end);
      j = anystart(s, (void *)redirectors);
      if (j) s += j;

      // Flow control characters that end pipeline segments
      else s = end + anystart(end, (char *[]){";;&", ";;", ";&", ";", "||",
        "|&", "|", "&&", "&", "(", ")", 0});
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

      // Match unquoted EOF.
      for (s = line, end = arg->v[arg->c]; *s && *end; s++, i++) {
        s += strspn(s, "\\\"'");
        if (*s != *end) break;
      }
      if (!*s && !*end) {
        // Add this line
        argxtend(arg);
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
// TODO <<< is funky
// argc[] entries removed from main list? Can have more than one?
        if (strcmp(s, "<<") && strcmp(s, "<<-") && strcmp(s, "<<<")) continue;
        if (i+1 == arg->c) goto flush;

        // Add another arg[] to the pipeline segment (removing/readding to list
        // because realloc can move pointer)
        dlist_lpop(&sp->pipeline);
        pl = xrealloc(pl, sizeof(*pl) + ++pl->count*sizeof(struct sh_arg));
        dlist_add_nomalloc((void *)&sp->pipeline, (void *)pl);

        // queue up HERE EOF so input loop asks for more lines.
        arg[pl->count].v = xzalloc(2*sizeof(void *));
        arg[pl->count].v[0] = arg->v[++i];
        arg[pl->count].v[1] = 0;
        arg[pl->count].c = 0;
        if (s[2] == '<') pl->here++; // <<< doesn't load more data
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

      // ;; and friends only allowed in case statements
      } else if (*s == ';' && (!ex || strcmp(ex, "esac"))) goto flush;
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
          else if (strcmp(s, "in")) goto flush;

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

  // ignore blank and comment lines
  if (!sp->pipeline) return 0;

// TODO <<< has no parsing impact, why play with it here at all?
  // advance past <<< arguments (stored as here documents, but no new input)
  pl = sp->pipeline->prev;
  while (pl->count<pl->here && pl->arg[pl->count].c<0)
    pl->arg[pl->count++].c = 0;

  // return if HERE document pending or more flow control needed to complete
  if (sp->expect) return 1;
  if (sp->pipeline && pl->count != pl->here) return 1;
  if (pl->arg->v[pl->arg->c]) return 1;

  // Don't need more input, can start executing.

  dlist_terminate(sp->pipeline);
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
      dprintf(2, "arg[%d][%ld]=%s\n", q, i, pl->arg->v[i]);
    dprintf(2, "type=%d term[%d]=%s\n", pl->type, q++, pl->arg->v[pl->arg->c]);
  }
}

/* Flow control statements:

  if/then/elif/else/fi, for select while until/do/done, case/esac,
  {/}, [[/]], (/), function assignment
*/


static int wait_pipeline(struct sh_process *pp)
{
  int rc = 0;

  for (dlist_terminate(pp); pp; pp = pp->next) {
    if (pp->pid) {
      // TODO job control: not xwait, handle EINTR ourselves and check signals
      pp->exit = xwaitpid(pp->pid);
      pp->pid = 0;
    }
    // TODO handle set -o pipefail here
    rc = pp->exit;
  }

  return rc;
}

static int pipe_segments(char *ctl, int *pipes, int **urd)
{
  unredirect(*urd);
  *urd = 0;

  // Did the previous pipe segment pipe input into us?
  if (pipes[1] != -1) {
    save_redirect(urd, pipes[1], 0);
    close(pipes[1]);
    pipes[1] = -1;
  }

  // are we piping output to the next segment?
  if (ctl && *ctl == '|' && ctl[1] != '|') {
    if (pipe(pipes)) {
      perror_msg("pipe");
// TODO record pipeline rc
// TODO check did not reach end of pipeline after loop
      return 1;
    }
    if (pipes[0] != 1) {
      save_redirect(urd, pipes[0], 1);
      close(pipes[0]);
    }
    fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
  }

  return 0;
}

static struct sh_pipeline *skip_andor(int rc, struct sh_pipeline *pl)
{
  char *ctl = pl->arg->v[pl->arg->c];

  // For && and || skip pipeline segment(s) based on return code
  while (ctl && ((!strcmp(ctl, "&&") && rc) || (!strcmp(ctl, "||") && !rc))) {
    if (!pl->next || pl->next->type == 2 || pl->next->type == 3) break;
    pl = pl->type ? block_end(pl) : pl->next;
    ctl = pl ? pl->arg->v[pl->arg->c] : 0;
  }

  return pl;
}

// run a parsed shell function. Handle flow control blocks and characters,
// setup pipes and block redirection, break/continue, call builtins,
// vfork/exec external commands.
static void run_function(struct sh_function *sp)
{
  struct sh_pipeline *pl = sp->pipeline, *end;
  struct blockstack {
    struct blockstack *next;
    struct sh_pipeline *start, *end;
    struct sh_process *pin;      // processes piping into this block
    int run, loop, *urd, pout;
    struct sh_arg farg;          // for/select arg stack
    struct string_list *fdelete; // farg's cleanup list
    char *fvar;                  // for/select's iteration variable name
  } *blk = 0, *new;
  struct sh_process *pplist = 0; // processes piping into current level
  int *urd = 0, pipes[2] = {-1, -1};
  long i;

// TODO can't free sh_process delete until ready to dispose else no debug output

  // iterate through pipeline segments
  while (pl) {
    char *s = *pl->arg->v, *ss = pl->arg->v[1];
    struct sh_process *pp = 0;

    // Is this an executable segment?
    if (!pl->type) {
      struct sh_arg *arg = pl->arg;
      char *ctl = arg->v[arg->c];

      // Skip disabled block
      if (blk && !blk->run) {
        while (pl->next && !pl->next->type) pl = pl->next;
        continue;
      }

      if (pipe_segments(ctl, pipes, &urd)) break;

      // If we just started a new pipeline, implicit parentheses (subshell)

// TODO: "echo | read i" is backgroundable with ctrl-Z despite read = builtin.
//       probably have to inline run_command here to do that? Implicit ()
//       also "X=42 | true; echo $X" doesn't get X.

      // TODO: bash supports "break &" and "break > file". No idea why.

      // Is it a flow control jump? These aren't handled as normal builtins
      // because they move *pl to other pipeline segments which is local here.
      if (!strcmp(s, "break") || !strcmp(s, "continue")) {

        // How many layers to peel off?
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
// TODO collate end_block logic

      // if ending a block, free, cleanup redirects, and pop stack.

          llist_traverse(blk->fdelete, free);
          unredirect(blk->urd);
          if (pipes[1]) close(pipes[1]);
          pipes[1] = blk->pout;
          free(llist_pop(&blk));
        }
        if (i) {
          syntax_err("break outside loop");
          break;
        }
        pl = pl->next;
        continue;

      // Parse and run next command
      } else {

// TODO: "echo | read i" is backgroundable with ctrl-Z despite read = builtin.
//       probably have to inline run_command here to do that? Implicit ()
//       also "X=42 | true; echo $X" doesn't get X.

        if (!(pp = run_command(arg))) break;
        dlist_add_nomalloc((void *)&pplist, (void *)pp);
      }

      if (pipes[1] == -1) {
        toys.exitval = wait_pipeline(pplist);
        llist_traverse(pplist, free_process);
        pplist = 0;
        pl = skip_andor(toys.exitval, pl);
      }

    // Start of flow control block?
    } else if (pl->type == 1) {

      // are we entering this block (rather than looping back to it)?
      if (!blk || blk->start != pl) {

        // If it's a nested block we're not running, skip ahead.
        end = block_end(pl->next);
        if (blk && !blk->run) {
          pl = end;
          if (pl) pl = pl->next;
          continue;
        }

        // If previous piped into this block, save context until block end
        if (pipe_segments(0, pipes, &urd)) break;

        // It's a new block we're running, save context and add it to the stack.
        new = xzalloc(sizeof(*blk));
        new->next = blk;
        blk = new;
        blk->start = pl;
        blk->end = end;
        blk->run = 1;

        // save context until block end
        blk->pout = pipes[1];
        blk->urd = urd;
        urd = 0;
        pipes[1] = -1;

        // Perform redirects listed at end of block
        pp = expand_redir(blk->end->arg, 0, blk->urd);
        if (pp) {
          blk->urd = pp->urd;
          if (pp->arg.c) syntax_err("unexpected %s", *pp->arg.v);
          llist_traverse(pp->delete, free);
          if (pp->arg.c) break;
          free(pp);
        }
      }

      // What flow control statement is this?

// TODO ( subshell

      // if/then/elif/else/fi, while until/do/done - no special handling needed

      // for select/do/done
      if (!strcmp(s, "for") || !strcmp(s, "select")) {
        if (blk->loop);
        else if (!strncmp(blk->fvar = ss, "((", 2)) {
          blk->loop = 1;
dprintf(2, "TODO skipped init for((;;)), need math parser\n");
        } else {

          // populate blk->farg with expanded arguments
          if (!pl->next->type) {
            for (i = 1; i<pl->next->arg->c; i++)
              expand_arg(&blk->farg, pl->next->arg->v[i], 0, &blk->fdelete);
          } else expand_arg(&blk->farg, "\"$@\"", 0, &blk->fdelete);
        }
        pl = pl->next;
      }

/* TODO
case/esac
{/}
[[/]]
(/)
((/))
function/}
*/

    // gearshift from block start to block body (end of flow control test)
    } else if (pl->type == 2) {

      // Handle if statement
      if (!strcmp(s, "then")) blk->run = blk->run && !toys.exitval;
      else if (!strcmp(s, "else") || !strcmp(s, "elif")) blk->run = !blk->run;
      else if (!strcmp(s, "do")) {
        ss = *blk->start->arg->v;
        if (!strcmp(ss, "while")) blk->run = blk->run && !toys.exitval;
        else if (!strcmp(ss, "until")) blk->run = blk->run && toys.exitval;
        else if (blk->loop >= blk->farg.c) {
          blk->run = 0;
          pl = block_end(pl);
          continue;
        } else if (!strncmp(blk->fvar, "((", 2)) {
dprintf(2, "TODO skipped running for((;;)), need math parser\n");
        } else setvar(xmprintf("%s=%s", blk->fvar, blk->farg.v[blk->loop++]),
          TAKE_MEM);
      }

    // end of block, may have trailing redirections and/or pipe
    } else if (pl->type == 3) {

      // repeating block?
      if (blk->run && !strcmp(s, "done")) {
        pl = blk->start;
        continue;
      }

// TODO goto "break" above instead of copying it here?
      // if ending a block, free, cleanup redirects, and pop stack.
      // needing to unredirect(urd) or close(pipes[1]) here would be syntax err
      llist_traverse(blk->fdelete, free);
      unredirect(blk->urd);
      pipes[1] = blk->pout;
      free(llist_pop(&blk));
    } else if (pl->type == 'f') pl = add_function(s, pl);

    pl = pl->next;
  }

  // did we exit with unfinished stuff?
  if (pipes[1] != -1) close(pipes[1]);
  if (pplist) {
    toys.exitval = wait_pipeline(pplist);
    llist_traverse(pplist, free_process);
  }
  unredirect(urd);

  // Cleanup from syntax_err();
  while (blk) {
    llist_traverse(blk->fdelete, free);
    unredirect(blk->urd);
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

void sh_main(void)
{
  FILE *f;
  char *new;
  struct sh_function scratch;
  int prompt = 0;

  // Is this an interactive shell?
//  if (FLAG(i) || (!FLAG(c)&&(FLAG(S)||!toys.optc) && isatty(0) && isatty(1))) 

  // Set up signal handlers and grab control of this tty.

  // Read environment for exports from parent shell
  subshell_imports();

  memset(&scratch, 0, sizeof(scratch));
  if (TT.command) f = fmemopen(TT.command, strlen(TT.command), "r");
  else if (*toys.optargs) f = xfopen(*toys.optargs, "r");
  else {
    f = stdin;
    if (isatty(0)) toys.optflags |= FLAG_i;
  }

  for (;;) {

    // Prompt and read line
    if (f == stdin) {
      char *s = getenv(prompt ? "PS2" : "PS1");

      if (!s) s = prompt ? "> " : (getpid() ? "\\$ " : "# ");
      do_prompt(s);
    } else TT.lineno++;
// TODO line editing/history
    if (!(new = xgetline(f ? f : stdin, 0))) break;
// TODO if (!isspace(*new)) add_to_history(line);

    // returns 0 if line consumed, command if it needs more data
    prompt = parse_line(new, &scratch);
if (0) dump_state(&scratch);
    if (prompt != 1) {
// TODO: ./blah.sh one two three: put one two three in scratch.arg
      if (!prompt) run_function(&scratch);
      free_function(&scratch);
      prompt = 0;
    }
    free(new);
  }

  if (prompt) error_exit("%ld:unfinished line"+4*!TT.lineno, TT.lineno);
  toys.exitval = f && ferror(f);
}
