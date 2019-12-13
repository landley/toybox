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

USE_SH(NEWTOY(sh, "c:i", TOYFLAG_BIN))
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
      struct string_list *delete;   // expanded strings
      struct sh_redirects {
        struct sh_redirects *next, *prev;
        int count, rd[];
      // rdlist = NULL if process didn't redirect, urd undoes <&- for builtins
      // rdlist is ** because this is our view into inherited context
      } **rdlist, *urd;
      int pid, exit;
      struct sh_arg arg;
    } *procs, *proc;
  } *jobs, *job;
  struct sh_process *callback_pp;
  unsigned jobcnt;
)

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

// cleanup one level of rdlist, can be llist_traverse() callback
static void free_redirects(void *redir)
{
  struct sh_redirects *rd = redir;
  int i, j;

  for (i = 0; i<rd->count; i++) {
    j = rd->rd[2*i+1];
    if (j&3) close(j>>2);  // close for parent process
  }

  free(rd);
}

// clean up struct sh_process
static void cleanup_process(struct sh_process *pp)
{
  int i, *rr;

  if (pp->rdlist) free_redirects(dlist_lpop((void *)pp->rdlist));
  llist_traverse(pp->delete, free);

  // restore stdin/out/err for interactive builtins
  if (pp->urd) for (i = 0; pp->urd->count; i++) {
    rr = pp->urd->rd+2*i;
    dup2(rr[0], rr[1]); // TODO fd exhaustion? (And do what about it?)
    close(rr[0]);
  }
}

int next_hfd(int hfd)
{
  for (; hfd<99999; hfd++) if (-1 == fcntl(hfd, F_GETFL)) break;
  return (hfd == 99999) ? -1 : hfd;
}

void add_redirect(struct sh_redirects **rdlist, int to, int from)
{
  struct sh_redirects *rd = *rdlist;
  int *rr, count;

  // if to and from both -1, add a redirect level instead of redirect entry
  if (to == -1 && from == -1) {
    rd = 0;
    count = 0;
  } else count = (rd = (*rdlist)->prev)->count;

  if (!rd || (count && !(count&31))) {
    if (rd) dlist_lpop((void *)rdlist);
    // add extra entry in case of |&
    dlist_add_nomalloc((void *)rdlist,
      xrealloc(rd, sizeof(*rd)+(count+32)*2*sizeof(int *)));
    if (!rd) return;
    rd = (*rdlist)->prev;
  }
  rr = rd->rd+2*count;
  rr[0] = to;
  rr[1] = from;
  rd->count++;
}

// Expand arguments and collect redirects. This can be called from command
// or block context.
static struct sh_process *expand_redir(struct sh_arg *arg, int envlen,
  struct sh_redirects **rdlist)
{
  struct sh_process *pp;
  char *s, *ss, *sss;
  int j, to, from, here = 0, hfd = 10;

  if (envlen<0 || envlen>=arg->c) return 0;
  pp = xzalloc(sizeof(struct sh_process));

  // We vfork() instead of fork to support nommu systems, and do
  // redirection setup in the parent process. Open new filehandles
  // and move to temporary values >10. Child calls dup2()/close after vfork().
  // If fd2 < 0 it's a here document (parent process writes to a pipe later).

  // Expand arguments and perform redirections
  for (j = envlen; j<arg->c; j++) {

    // Is this a redirect? s = prefix, ss = operator
    sss = ss = (s = arg->v[j]) + redir_prefix(arg->v[j]);
    sss += anystart(ss, (char *[]){"<<<", "<<-", "<<", "<&", "<>", "<", ">>",
      ">&", ">|", ">", "&>>", "&>", 0});
    if (ss == sss) {
      // Nope: save/expand argument and loop
      expand_arg(&pp->arg, s, 0, &pp->delete);

      continue;
    } else if (j+1 >= arg->c) {
      s = "\\n";
      goto flush;
    }
    sss = arg->v[++j];

    // It's a redirect: for [fd]<name s = start of [fd], ss = <, sss = name

    if (!pp->rdlist) add_redirect(pp->rdlist = rdlist, -1, -1);
    hfd = next_hfd(hfd);
    // error check: premature EOF, no free high fd, target fd too big
    if (hfd == -1 || ++j == arg->c || (isdigit(*s) && ss-s>5)) goto flush;

    // expand arguments for everything but << and <<-
    if (strncmp(ss, "<<", 2) && ss[2] != '<') {
      sss = expand_one_arg(sss, NO_PATH);
      if (!sss) goto flush; // arg splitting here is an error
      if (sss != arg->v[j]) dlist_add((void *)&pp->delete, sss);
    }

    // Parse the [fd] part of [fd]<name
    to = *ss != '<';
    if (isdigit(*s)) to = atoi(s);
    else if (*s == '{') {
      // when we close a filehandle, we _read_ from {var}, not write to it
      if ((!strcmp(ss, "<&") || !strcmp(ss, ">&")) && !strcmp(sss, "-")) {
        to = -1;
        if ((ss = getvar(s+1, ss-s-2))) to = atoi(ss); // TODO trailing garbage?
        if (to<0) goto flush;
        add_redirect(rdlist, to, (to<<2)+1);

        continue;
      // record high file descriptor in {to}<from environment variable
      } else setvar(xmprintf("%.*s=%d", (int)(ss-s-1), s, to = hfd),  TAKE_MEM);
    }

    // HERE documents?
    if (!strcmp(ss, "<<<") || !strcmp(ss, "<<-") || !strcmp(ss, "<<")) {
      char *tmp = getvar("TMPDIR", 6);
      int i, bad, len, l2, zap = (ss[2] == '-'),
        noforg =(ss[strcspn(ss, "\"'")]);

      // store contents in open-but-deleted /tmp file.
      tmp = xmprintf("%s/sh-XXXXXX", tmp ? tmp : "/tmp");
      if ((from = mkstemp(tmp))>=0) {
        if (unlink(tmp)) bad++;

        // write here document contents to file and lseek back to start
        else if (ss[2] == '<') {
          if (!noforg) sss = expand_one_arg(sss, NO_PATH|NO_SPLIT);
          len = strlen(sss);
          if (len != writeall(from, sss, len)) bad++;
          free(sss);
        } else {
          struct sh_arg *hh = arg+here++;

          for (i = 0; i<hh->c; i++) {
            ss = hh->v[i];
            sss = 0;
            // expand_parameter, commands, and arithmetic
            if (!noforg)
              ss = sss = expand_one_arg(ss,
                NO_PATH|NO_SPLIT|NO_BRACE|NO_TILDE|NO_QUOTE);

            while (zap && *ss == '\t') ss++;
            l2 = writeall(from, ss, len = strlen(ss));
            free(sss);
            if (len != l2) break;
          }
          if (i != hh->c) bad++;
        }
        if (!bad && lseek(from, 0, SEEK_SET)) bad++;
      }

      // error report/handling
      if (bad || from == -1 || hfd != dup2(from, hfd)) {
        if (bad || from == -1) perror_msg("bad %s: '%s'", ss, tmp);
        else perror_msg("dup2");
        if (from != -1) close(from);
        pp->exit = 1;
        s = 0;
        free(tmp);

        goto flush;
      }
      free(tmp);

      if (from != hfd) close(from);
      add_redirect(rdlist, to, (from<<2)+(2*(to!=from)));

      continue;
    }

    // from>=0 means it's fd<<2 (new fd to dup2() after vfork()) plus
    // 2 if we should close(from>>2) after dup2(from>>2, to),
    // 1 if we should close but dup for nofork recovery (ala <&2-)

    // Handle file descriptor duplication/close (&> &>> <& >& with number or -)
    // These redirect existing fd so nothing to open()
    if (strchr(ss, '&') && ss[2] != '>' && *ss != '|') {
      // is there an explicit fd?
      ss = sss;
      while (isdigit(ss)) ss++;
      if (ss-sss>5 || (*ss && (*ss != '-' || ss[1]))) {
        // bad fd
        s = sss;
        goto flush;
      }

      // TODO can't reasonably check if fd is open here, should
      // do it when actual redirects happen
      add_redirect(rdlist, to, (((ss==sss)?to:atoi(sss))<<2)+(*ss != '-'));

      continue;
    }

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
    from = xcreate(sss, from|WARN_ONLY, 777);
    if (-1 == from || hfd != dup2(from, hfd)) {
      pp->exit = 1;
      s = 0;
      if (from != -1) perror_msg("dup2");

      goto flush;
    }
    if (from != hfd) close(from);

    add_redirect(rdlist, to, (hfd<<2)+2);
  }

  s = 0;

flush:
  if (s) {
    syntax_err("bad %s", s);
    if (!pp->exit) pp->exit = 1;
  }

  return pp;
}

// perform the redirects in an rdlist, saving undo information as necessary
// rd->rd[] is destination/source filehandle pairs, length is 2*rd->count
// first (dest): filehandle to replace (via dup2)
// second (src): fd<<2 + 2=close fd after dup, 1=close but save for nofork
static int perform_redirects(struct sh_process *pp, int nofork)
{
  struct sh_redirects *rd = 0;
  int rc = 0, hfd = 20;

  if (pp->rdlist) rd = *pp->rdlist;
  if (rd) for (;;) {
    int i, j, *rr;

    for (i = 0; i<rd->count; i++) {
      rr = rd->rd+2*i;

      // preserve redirected stdin/out/err for nofork, to restore later
      if (nofork && (rr[1]&1)) {
        if (!pp->urd) add_redirect(&pp->urd, -1, -1);
        hfd = next_hfd(hfd);
        if (hfd == -1 || hfd != dup2(rr[0], hfd)) {
          perror_msg("%d", rr[0]);
          rc = 1;
          continue; // don't perform a redirect we can't undo
        } else add_redirect(&pp->urd, hfd, rr[0]);
      }

      // move the filehandle
      j = rr[1]>>2;
      if (rr[0] != j && j != dup2(rr[0], j)) {
        perror_msg("%d", j);
        rc = 1;
      } else if ((rr[1]&1) || ((rr[1]&2) && !nofork)) {
        close(j);
        rr[1] &= ~2;
      }
    }

    if (rd->next == *pp->rdlist) break;
    rd = rd->next;
  }

  return rc;
}

// callback from xpopen_setup()
static void redirect_callback(void)
{
  if (perform_redirects(TT.callback_pp, 0)) _exit(1);
  TT.callback_pp = 0;
}

// Execute the commands in a pipeline segment
static struct sh_process *run_command(struct sh_arg *arg,
  struct sh_redirects **rdlist, int *pipes)
{
  struct sh_process *pp;
  struct toy_list *tl;

  // grab environment var assignments, expand arguments and queue up redirects
  if (!(pp = expand_redir(arg, assign_env(arg), rdlist))) return 0;
  if (pp->exit) return pp;

// TODO: handle ((math))
// TODO: check for functions()

  // Is this command a builtin that should run in this process?
  if ((tl = toy_find(*pp->arg.v))
    && (tl->flags & (TOYFLAG_NOFORK|TOYFLAG_MAYFORK)))
  {
    struct toy_context temp;
    sigjmp_buf rebound;

    // NOFORK can't background and blocks until done or interrupted, so
    // do redirects here then unwind after the command.

    perform_redirects(pp, 1);

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
    TT.callback_pp = pp;
    if (-1 == (pp->pid = xpopen_setup(pp->arg.v, pipes, redirect_callback)))
      perror_msg("%s: vfork", *pp->arg.v);
  }
  cleanup_process(pp);

  // unwind redirects

// TODO: what if exception handler recovery?

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
int run_pipeline(struct sh_pipeline **pl, struct sh_redirects **rdlist)
{
  struct sh_process *pp;
  int rc = 0, pipes[2];

  for (;;) {
// TODO job control
// TODO pipes (ending, leading)
    if (!(pp = run_command((*pl)->arg, rdlist, 0))) rc = 0;
    else {
// TODO backgrounding
      if (pp->pid) pp->exit = xpclose_both(pp->pid, 0);
//wait4(pp);
// TODO -o pipefail
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

  // advance past <<< arguments (stored as here documents, but no new input)
  if (!sp->pipeline) return 0;
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

/* Flow control statements:

  if/then/elif/else/fi, for select while until/do/done, case/esac,
  {/}, [[/]], (/), function assignment
*/



// run a shell function, handling flow control statements
static void run_function(struct sh_function *sp)
{
  struct sh_pipeline *pl = sp->pipeline, *end;
  struct blockstack {
    struct blockstack *next;
    struct sh_pipeline *start, *end;
    struct sh_redirects *redir;
    int run, loop;

    struct sh_arg farg;          // for/select arg stack
    struct string_list *fdelete; // farg's cleanup list
    char *fvar;                  // for/select's iteration variable name
  } *blk = 0, *new;
  long i;

  // iterate through the commands
  while (pl) {
    char *s = *pl->arg->v, *ss = pl->arg->v[1];
//dprintf(2, "s=%s %s %d %s %d\n", s, ss, pl->type, blk ? blk->start->arg->v[0] : "X", blk ? blk->run : 0);
    // Normal executable statement?
    if (!pl->type) {
// TODO: break & is supported? Seriously? Also break > potato
// TODO: break multiple aguments
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
          llist_traverse(blk->fdelete, free);
          free(llist_pop(&blk));
        }
        pl = pl->next;

        continue;
      }

// inherit redirects?
// returns last statement of pipeline
      if (!blk) toys.exitval = run_pipeline(&pl, 0);
      else if (blk->run) toys.exitval = run_pipeline(&pl, &blk->redir);
      else while (pl->next && !pl->next->type) pl = pl->next;

    // Starting a new block?
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

        // It's a new block we're running, save context and add it to the stack.
        new = xzalloc(sizeof(*blk));
        new->next = blk;
        blk = new;
        blk->start = pl;
        blk->end = end;
        blk->run = 1;
// TODO perform block end redirects to blk->redir
      }

      // What flow control statement is this?

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

    // gearshift from block start to block body
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

// TODO unwind redirects (cleanup blk->redir)

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
