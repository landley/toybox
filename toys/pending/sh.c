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
 * deviations from posix: don't care about $LANG or $LC_ALL

 * TODO: test that $PS1 color changes work without stupid \[ \] hack
 * TODO: Handle embedded NUL bytes in the command line? (When/how?)
 * TODO: replace getenv() with faster func: sort env and binary search

 * buitins: alias bg command fc fg getopts jobs newgrp read umask unalias wait
 *          disown umask suspend source pushd popd dirs logout times trap
 *          unset local export readonly set : . let history declare
 * "special" builtins: break continue eval exec return shift
 * builtins with extra shell behavior: kill pwd time test

 * | & ; < > ( ) $ ` \ " ' <space> <tab> <newline>
 * * ? [ # ~ = %
 * ! { } case do done elif else esac fi for if in then until while
 * [[ ]] function select

 * label:
 * TODO: test exit from "trap EXIT" doesn't recurse
 * TODO: ! history expansion
 * TODO: getuid() vs geteuid()
 *
 * bash man page:
 * control operators || & && ; ;; ;& ;;& ( ) | |& <newline>
 * reserved words
 *   ! case  coproc  do done elif else esac fi for  function  if  in  select
 *   then until while { } time [[ ]]

USE_SH(NEWTOY(cd, ">1LP[-LP]", TOYFLAG_NOFORK))
USE_SH(NEWTOY(exit, 0, TOYFLAG_NOFORK))

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
  char *c;

  long lineno;
  char **locals, *subshell_env;
  struct double_list functions;
  unsigned options, jobcnt, loc_ro, loc_magic;
  int hfd;  // next high filehandle (>= 10)

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
      struct arg_list *delete;   // expanded strings
      int *urd, envlen, pid, exit;  // undo redirects, child PID, exit status
      struct sh_arg arg;
    } *procs, *proc;
  } *jobs, *job;
)

#define BUGBUG 0

// ordered for greedy matching, so >&; becomes >& ; not > &;
// making these const means I need to typecast the const away later to
// avoid endless warnings.
static const char *redirectors[] = {"<<<", "<<-", "<<", "<&", "<>", "<", ">>",
  ">&", ">|", ">", "&>>", "&>", 0};

#define SH_NOCLOBBER 1   // set -C

// like error_msg() but exit from shell scripts
static void syntax_err(char *msg, ...)
{
  va_list va;

// TODO rethink syntax errordom
  va_start(va, msg);
  verror_msg(msg, 0, va);
  va_end(va);

  if (*toys.optargs) xexit();
}

// append to array with null terminator and realloc as necessary
void array_add(char ***list, unsigned count, char *data)
{
  if (!(count&31)) *list = xrealloc(*list, sizeof(char *)*(count+33));
  (*list)[count] = data;
  (*list)[count+1] = 0;
}

// Return index of variable within this list
static unsigned findvar(char **list, char *name, int len)
{
  unsigned i;

  for (i = 0; list[i]; i++)
    if (!strncmp(list[i], name, len) && list[i][len] == '=') break;

  return i;
}

// Assign one variable
// s: key=val
// type: 0 = whatever it was before, local otherwise
#define TAKE_MEM 0x80000000
// declare -aAilnrux
// ft
static void setvar(char *s, unsigned type)
{
  unsigned uu;
  int len = stridx(s, '=');

  if (len == -1) return error_msg("no = in setvar %s\n", s);

  if (type&TAKE_MEM) type ^= TAKE_MEM;
  else s = xstrdup(s);

  // local, export, readonly, integer...

  // exported variable?
  if (environ && environ[uu = findvar(environ, s, len)]) {
    if (uu>=toys.envc) free(environ[uu]);
    environ[uu] = s;
  } else if (TT.locals[uu = findvar(TT.locals, s, len)]) {
    if (uu<TT.loc_ro) return error_msg("%.*s: readonly variable", len, s);
    free(TT.locals[uu]);
    TT.locals[uu] = s;
  } else array_add(&TT.locals, uu, s);
}

// get variable of length len starting at s.
static char *getvarlen(char *s, int len)
{
  int i;

  if (TT.locals && TT.locals[i = findvar(TT.locals, s, len)])
    return TT.locals[i]+len+1;
  if (environ && environ[i = findvar(environ, s, len)])
    return environ[i]+len+1;

  return 0;
}

static char *getvar(char *s)
{
  return getvarlen(s, strlen(s));
}

// returns offset of next unquoted (or double quoted if dquot) char.
// handles \ '' "" `` $()
int skip_quote(char *s, int dquot, int *depth)
{
  int i, q = dquot ? *depth : 0;

  // quotes were checked for balance and overflow by parse_word()
  for (i = 0; s[i]; i++) {
    char c = s[i], qq = q ? toybuf[q-1] : 0;

    if (c == '\\') i++;
    else if (dquot && q==1 && qq=='"' && c!='"') break;
    else if (qq!='\'' && c=='$' && s[1]=='(') {
      toybuf[q++] = ')';
      i++;
    } else if (q && qq==c) q--;
    else if ((!q || qq==')') && (c=='"' || c=='\'' || c=='`')) toybuf[q++] = c;
    else if (!q) break;
  }

  if (dquot) *depth = q;

  return i;
}

// add argument to an arg_list
void add_arg(struct arg_list **list, char *arg)
{
  struct arg_list *al;

  if (!list) return;
  al = xmalloc(sizeof(struct arg_list));
  al->next = *list;
  al->arg = arg;
  *list = al;
}

#define NO_PATH  (1<<0)    // path expansion (wildcards)
#define NO_SPLIT (1<<1)    // word splitting
#define NO_BRACE (1<<2)    // {brace,expansion}
#define NO_TILDE (1<<3)    // ~username/path
#define NO_QUOTE (1<<4)    // quote removal
#define FORCE_COPY (1<<31) // don't keep original, copy even if not modified
#define FORCE_KEEP (1<<30) // this is a copy, free if not appended to delete
// TODO: parameter/variable $(command) $((math)) split pathglob
// TODO: ${name:?error} causes an error/abort here (syntax_err longjmp?)
// TODO: $1 $@ $* need args marshalled down here: function+structure?
// arg = append to this
// new = string to expand
// flags = type of expansions (not) to do
// delete = append new allocations to this so they can be freed later
// TODO: at_args: $1 $2 $3 $* $@
static void expand_arg_nobrace(struct sh_arg *arg, char *old, unsigned flags,
  struct arg_list **delete)
{
  char *new = old, *s, *ss, *sss;

  if (flags&FORCE_KEEP) old = 0;

// TODO ls -l /proc/$$/fd

  // Tilde expansion
  if (!(flags&NO_TILDE) && *new == '~') {
    struct passwd *pw = 0;

    // first expansion so don't need to free previous new
    ss = 0;
    for (s = new; *s && *s!=':' && *s!='/'; s++);
    if (s-new==1) {
      if (!(ss = getvar("HOME")) || !*ss) pw = bufgetpwuid(getuid());
    } else {
      // TODO bufgetpwnam
      pw = getpwnam(sss = xstrndup(new+1, (s-new)-1));
      free(sss);
    }
    if (pw && pw->pw_dir) ss = pw->pw_dir;
    if (!ss || !*ss) ss = "/";
    s = xmprintf("%s%s", ss, s);
    if (old != new) free(new);
    new = s;
  }

  // parameter/variable expansion

// TODO this is wrong
  if (*new == '$') {
    char *s = getvar(new+1);

    if (new != old) free(new);
    if (!s) return;
    new = xstrdup(s);
  }

/*
  for (s = new; *(s += skip_quote(s, 1, &depth));) {
    if (*s == '`') {

// ${ $(( $( $[ $' ` " '

  while (*s) {
    if (quote != '*s == '$') {
      // *@#?-$!_0 "Special Paremeters" ($0 not affected by shift)
      // 0-9 positional parameters
      if (s[1] == '$'
    }
  }

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
*/

// TODO not else?
  // quote removal
  else if (!(flags&NO_QUOTE)) {
    int to = 0, from = 0;

    for (;;) {
      char c = new[from++];

      if (c == '"' || c=='\'') continue;
      if (c == '\\' && new[from]) c = new[from++];
      if (from != to && old == new) new = xstrdup(new);
      if (!(new[to++] = c)) break;
    }
  }

  // Record result.
  if (old==new && (flags&FORCE_COPY)) new = xstrdup(new);
  if (old!=new) add_arg(delete, new);
  array_add(&arg->v, arg->c++, new);
}

// expand braces (ala {a,b,c}) and call expand_arg_nobrace() each permutation
static void expand_arg(struct sh_arg *arg, char *old, unsigned flags,
  struct arg_list **delete)
{
  struct brace {
    struct brace *next, *prev, *stack;
    int active, cnt, idx, commas[];
  } *bb = 0, *blist = 0, *bstk, *bnext;
  int i, j;
  char *s, *ss;

  // collect brace spans
  if (!(flags&NO_BRACE)) for (i = 0; ; i++) {
    i += skip_quote(old+i, 0, 0);
    if (!bb && !old[i]) break;
    if (bb && (!old[i] || old[i] == '}')) {
      bb->active = bb->commas[bb->cnt+1] = i;
      for (bnext = bb; bb && bb->active; bb = (bb==blist)?0:bb->prev);
      if (!old[i] || !bnext->cnt) // discard commaless brace from start/middle
        free(dlist_pop((blist == bnext) ? &blist : &bnext));
    } else if (old[i] == '{') {
      dlist_add_nomalloc((void *)&blist,
        (void *)(bb = xzalloc(sizeof(struct brace)+34*4)));
      bb->commas[0] = i;
    } else if (!bb) continue;
    else if  (bb && old[i] == ',') {
      if (bb->cnt && !(bb->cnt&31)) {
        dlist_lpop(&blist);
        dlist_add_nomalloc((void *)&blist,
          (void *)(bb = xrealloc(bb, sizeof(struct brace)+(bb->cnt+34)*4)));
      }
      bb->commas[++bb->cnt] = i;
    }
  }

// TODO NOSPLIT with braces? (Collate with spaces?)
  // If none, pass on verbatim
  if (!blist) return expand_arg_nobrace(arg, old, flags, delete);

  // enclose entire range in top level brace.
  (bstk = xzalloc(sizeof(struct brace)+8))->commas[1] = strlen(old)+1;
  bstk->commas[0] = -1;

  // loop through each combination
  for (;;) {

    // Brace expansion can't be longer than original string. Keep start to {
    s = ss = xmalloc(bstk->commas[1]);

    // Append output from active braces (in "saved" list)
    for (bb = blist; bb;) {

      // keep prefix and push self onto stack
      if (bstk == bb) bstk = bstk->stack;  // pop self
      i = bstk->commas[bstk->idx]+1;
      if (bstk->commas[bstk->cnt+1]>bb->commas[0])
        s = stpncpy(s, old+i, bb->commas[0]-i);

      // pop sibling
      if (bstk->commas[bstk->cnt+1]<bb->commas[0]) bstk = bstk->stack;
 
      bb->stack = bstk; // push
      bb->active = 1;
      bstk = bnext = bb;

      // skip inactive spans from earlier or later commas
      while ((bnext = (bnext->next==blist) ? 0 : bnext->next)) {
        i = bnext->commas[0];

        // past end of this brace
        if (i>bb->commas[bb->cnt+1]) break;

        // in this brace but not this selection
        if (i<bb->commas[bb->idx] || i>bb->commas[bb->idx+1]) {
          bnext->active = 0;
          bnext->stack = 0;

        // in this selection
        } else break;
      }

      // is next span past this range?
      if (!bnext || bnext->commas[0]>bb->commas[bb->idx+1]) {

        // output uninterrupted span
        i = bb->commas[bstk->idx]+1;
        s = stpncpy(s, old+i, bb->commas[bb->idx+1]-i);

        // While not sibling, output tail and pop
        while (!bnext || bnext->commas[0] > bstk->commas[bstk->cnt+1]) {
          if (!(bb = bstk->stack)) break;
          i = bstk->commas[bstk->cnt+1]+1; // start of span
          j = bb->commas[bb->idx+1]; // enclosing comma span

          while (bnext) {
            if (bnext->commas[0]<j) {
              j = bnext->commas[0];// sibling
              break;
            } else if (bb->commas[bb->cnt+1]>bnext->commas[0])
              bnext = (bnext->next == blist) ? 0 : bnext->next;
            else break;
          }
          s = stpncpy(s, old+i, j-i);

          // if next is sibling but parent _not_ a sibling, don't pop
          if (bnext && bnext->commas[0]<bstk->stack->commas[bstk->stack->cnt+1])
            break;
          bstk = bstk->stack;
        }
      }
      bb = (bnext == blist) ? 0 : bnext;
    }

    // Save result
    expand_arg_nobrace(arg, ss, flags|FORCE_KEEP, delete);

    // increment
    for (bb = blist->prev; bb; bb = (bb == blist) ? 0 : bb->prev) {
      if (!bb->stack) continue;
      else if (++bb->idx > bb->cnt) bb->idx = 0;
      else break;
    }

    // if increment went off left edge, done expanding
    if (!bb) return llist_traverse(blist, free);
  }
}

// Expand exactly one arg, returning NULL if it split.
static char *expand_one_arg(char *new, unsigned flags, struct arg_list **del)
{
  struct sh_arg arg;
  char *s = 0;
  int i;

  memset(&arg, 0, sizeof(arg));
  expand_arg(&arg, new, flags, del);
  if (arg.c == 1) s = *arg.v;
  else if (!del) for (i = 0; i < arg.c; i++) free(arg.v[i]);
  free(arg.v);

  return s;
}

// return length of match found at this point (try is null terminated array)
static int anystart(char *s, char **try)
{
  char *ss = s;

  while (*try) if (strstart(&s, *try++)) return s-ss;

  return 0;
}

// does this entire string match one of the strings in try[]
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

// restore displaced filehandles, closing high filehandles they were copied to
static void unredirect(int *urd)
{
  int *rr = urd+1, i;

  if (!urd) return;

  for (i = 0; i<*urd; i++, rr += 2) {
if (BUGBUG) dprintf(255, "urd %d %d\n", rr[0], rr[1]);
    if (rr[1] != -1) {
      // No idea what to do about fd exhaustion here, so Steinbach's Guideline.
      dup2(rr[0], rr[1]);
      close(rr[0]);
    }
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
  if ((hfd = next_hfd())==-1) return 1;
  if (hfd != dup2(to, hfd)) hfd = -1;
  else fcntl(hfd, F_SETFD, FD_CLOEXEC);

if (BUGBUG) dprintf(255, "%d redir from=%d to=%d hfd=%d\n", getpid(), from, to, hfd);
  // dup "to"
  if (from != -1 && to != dup2(from, to)) {
    if (hfd != -1) close(hfd);

    return 1;
  }

  // Append undo information to redirect list so we can restore saved hfd later.
  if (!((cnt = *rd ? **rd : 0)&31)) *rd = xrealloc(*rd, (cnt+33)*2*sizeof(int));
  *(rr = *rd) = ++cnt;
  rr[2*cnt-1] = hfd;
  rr[2*cnt] = to;

  return 0;
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

// TODO: waitpid(WNOHANG) to clean up zombies and catch background& ending

static void subshell_callback(void)
{
  TT.subshell_env = xmprintf("@%d,%d=", getpid(), getppid());
  xsetenv(TT.subshell_env, 0);
  TT.subshell_env[strlen(TT.subshell_env)-1] = 0;
}

// TODO avoid prototype
static int sh_run(char *new);

// Pass environment and command string to child shell, return PID of child
static int run_subshell(char *str, int len)
{
  pid_t pid;

  // The with-mmu path is significantly faster.
  if (CFG_TOYBOX_FORK) {
    char *s;

    if ((pid = fork())<0) perror_msg("fork");
    else if (!pid) {
      s = xstrndup(str, len);
      sh_run(s);
      free(s);

      _exit(toys.exitval);
    }

  // On nommu vfork, exec /proc/self/exe, and pipe state data to ourselves.
  } else {
    int pipes[2], i;

    // open pipe to child
    if (pipe(pipes) || 254 != dup2(pipes[0], 254)) return 1;
    close(pipes[0]);
    fcntl(pipes[1], F_SETFD, FD_CLOEXEC);

    // vfork child
    pid = xpopen_setup(0, 0, subshell_callback);

    // marshall data to child
    close(254);
    if (TT.locals)
      for (i = 0; TT.locals[i]; i++) dprintf(pipes[1], "%s\n", TT.locals[i]);
    dprintf(pipes[1], "%.*s\n", len, str);
    close(pipes[1]);
  }

  return pid;
}

// turn a parsed pipeline back into a string.
static char *pl2str(struct sh_pipeline *pl)
{
  struct sh_pipeline *end = 0;
  int level = 0, len = 0, i, j;
  char *s, *ss, *sss;

  // measure, then allocate
  for (j = 0; ; j++) for (end = pl; end; end = end->next) {
    if (end->type == 1) level++;
    else if (end->type == 3 && --level<0) break;

    for (i = 0; i<pl->arg->c; i++)
      if (j) ss += sprintf(ss, "%s ", pl->arg->v[i]);
      else len += strlen(pl->arg->v[i])+1;

    sss = pl->arg->v[pl->arg->c];
    if (!sss) sss = ";";
    if (j) ss = stpcpy(ss, sss);
    else len += strlen(sss);

// TODO add HERE documents back in
    if (j) return s;
    s = ss = xmalloc(len+1);
  }
}

// Expand arguments and perform redirections. Return new process object with
// expanded args. This can be called from command or block context.
static struct sh_process *expand_redir(struct sh_arg *arg, int envlen, int *urd)
{
  struct sh_process *pp;
  char *s = s, *ss, *sss, *cv = 0;
  int j, to, from, here = 0;

  TT.hfd = 10;

  pp = xzalloc(sizeof(struct sh_process));
  pp->urd = urd;

  // When we redirect, we copy each displaced filehandle to restore it later.

  // Expand arguments and perform redirections
  for (j = envlen; j<arg->c; j++) {
    int saveclose = 0, bad = 0;

    s = arg->v[j];

    // Handle <() >() redirectionss
    if ((*s == '<' || *s == '>') && s[1] == '(') {
      int pipes[2], *uu = 0, dd;

      // Grab subshell data
      if (pipe(pipes)) {
        perror_msg_raw(s);
        pp->exit = 1;

        return pp;
      }

      // Perform input or output redirect and launch process
      dd = *s == '<';
      save_redirect(&uu, pipes[dd], dd);
      close(pipes[dd]);
      run_subshell(s+2, strlen(s+2)-1); // ignore errors, don't track
      unredirect(uu);
      save_redirect(&urd, -1, pipes[!dd]);

      // bash uses /dev/fd/%d which requires /dev/fd to be a symlink to
      // /proc/self/fd so we just produce that directly.
      add_arg(&pp->delete, ss = xmprintf("/proc/self/fd/%d", pipes[!dd]));
      array_add(&pp->arg.v, pp->arg.c++, ss);

      continue;
    }

    // Is this a redirect? s = prefix, ss = operator
    ss = s + redir_prefix(arg->v[j]);
    sss = ss + anystart(ss, (void *)redirectors);
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
    if (strncmp(ss, "<<", 2) && ss[2] != '<' &&
      !(sss = expand_one_arg(sss, NO_PATH, &pp->delete)))
    {
      s = sss;
      break; // arg splitting here is an error
    }

    // Parse the [fd] part of [fd]<name
    to = *ss != '<';
    if (isdigit(*s)) to = atoi(s);
    else if (*s == '{') {
      // when we close a filehandle, we _read_ from {var}, not write to it
      if ((!strcmp(ss, "<&") || !strcmp(ss, ">&")) && !strcmp(sss, "-")) {
        if (!(ss = getvarlen(s+1, ss-s-2))) break;
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
      char *tmp = getvar("TMPDIR");
      int i, len, zap = (ss[2] == '-'), x = !ss[strcspn(ss, "\"'")];

      // store contents in open-but-deleted /tmp file.
      tmp = xmprintf("%s/sh-XXXXXX", tmp ? tmp : "/tmp");
      if ((from = mkstemp(tmp))>=0) {
        if (unlink(tmp)) bad++;

        // write contents to file (if <<< else <<) then lseek back to start
        else if (ss[2] == '<') {
          if (x) sss = expand_one_arg(sss, NO_PATH|NO_SPLIT, 0);
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
              NO_PATH|NO_SPLIT|NO_BRACE|NO_TILDE|NO_QUOTE, 0);

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

    // from is fd<<2 (new fd to dup2() after vfork()) plus
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
      saveclose = 2-(*ss == '-');
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

      // we expect /dev/fd/# and /dev/{stdin,stdout,stderr} to be in /dev

// TODO: /dev/{tcp,udp}/host/port

      // Open the file
      if (-1 == (from = xcreate(sss, from|WARN_ONLY, 0666))) break;
    }

    // perform redirect, saving displaced "to".
    if (save_redirect(&pp->urd, from, to)) bad++;
    // Do we save displaced "to" in env variable instead of undo list?
    if (cv) {
      --*pp->urd;
      setvar(cv, TAKE_MEM);
      cv = 0;
    }
    if ((saveclose&1) && save_redirect(&pp->urd, -1, from)) bad++;
    if (!(saveclose&2)) close(from);
    if (bad) break;
  }

  // didn't parse everything?
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
  int envlen, j;
  char *s;

  // Grab leading variable assignments
  for (envlen = 0; envlen<arg->c; envlen++) {
    s = arg->v[envlen];
    for (j=0; s[j] && (s[j]=='_' || !ispunct(s[j])); j++);
    if (!j || s[j] != '=') break;
  }

  // expand arguments and perform redirects
  pp = expand_redir(arg, envlen, 0);

if (BUGBUG) { int i; dprintf(255, "envlen=%d arg->c=%d run=", envlen, arg->c); for (i=0; i<pp->arg.c; i++) dprintf(255, "'%s' ", pp->arg.v[i]); dprintf(255, "\n"); }
  // perform assignments locally if there's no command
  if (envlen == arg->c) {
    for (j = 0; j<envlen; j++) {
      s = expand_one_arg(arg->v[j], NO_PATH|NO_SPLIT, 0);
      setvar(s, TAKE_MEM*(s!=arg->v[j]));
    }

  // Do nothing if nothing to do
  } else if (pp->exit || !pp->arg.v);
//  else if (!strcmp(*pp->arg.v, "(("))
// TODO: handle ((math)) currently totally broken
// TODO: call functions()
  // Is this command a builtin that should run in this process?
  else if ((tl = toy_find(*pp->arg.v))
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
      xflush(0);
    }
    pp->exit = toys.exitval;
    if (toys.optargs != toys.argv+1) free(toys.optargs);
    if (toys.old_umask) umask(toys.old_umask);
    memcpy(&toys, &temp, sizeof(struct toy_context));
  } else {
    char **env = 0, **old = environ, *ss, *sss;
    int kk = 0, ll;

    // We don't allocate/free any array members, just the array
    if (environ) while (environ[kk]) kk++;
    if (kk) {
      env = xmalloc(sizeof(char *)*(kk+33));
      memcpy(env, environ, sizeof(char *)*(kk+1));
      environ = env;
    }
    // assign leading environment variables
    for (j = 0; j<envlen; j++) {
      sss = expand_one_arg(arg->v[j], NO_PATH|NO_SPLIT, &pp->delete);
      for (ll = 0; ll<kk; ll++) {
        for (s = sss, ss = env[ll]; *s == *ss && *s != '='; s++, ss++);
        if (*s != '=') continue;
        env[ll] = sss;
        break;
      }
      if (ll == kk) array_add(&environ, kk++, sss);
    }
    ss = getvar("SHLVL");
    sprintf(toybuf, "%d", atoi(ss ? ss : "")+1);
    xsetenv("SHLVL", toybuf);

    if (-1 == (pp->pid = xpopen_both(pp->arg.v, 0)))
      perror_msg("%s: vfork", *pp->arg.v);

    // Restore environment variables
    environ = old;
    free(env);
  }

  // cleanup process
  unredirect(pp->urd);

  return pp;
}

static void free_process(void *ppp)
{
  struct sh_process *pp = ppp;
  llist_traverse(pp->delete, llist_free_arg);
  free(pp);
}


// parse next word from command line. Returns end, or 0 if need continuation
// caller eats leading spaces
static char *parse_word(char *start)
{
  int i, quote = 0, q, qc = 0;
  char *end = start, *s;

  // Things we should only return at the _start_ of a word

  if (strstart(&end, "<(") || strstart(&end, ">(")) toybuf[quote++]=')';

  // Redirections. 123<<file- parses as 2 args: "123<<" "file-".
  s = end + redir_prefix(end);
  if ((i = anystart(s, (void *)redirectors))) s += i;
  if (s != end) return (end == start) ? s : end;

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

    // Handle quote contexts
    if ((q = quote ? toybuf[quote-1] : 0)) {

      // when waiting for parentheses, they nest
      if ((q == ')' || q == '\xff') && (*end == '(' || *end == ')')) {
        if (*end == '(') qc++;
        else if (qc) qc--;
        else if (q == '\xff') {
          // (( can end with )) or retroactively become two (( if we hit one )
          if (strstart(&end, "))")) quote--;
          else return start+1;
        } else if (*end == ')') quote--;
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
      if (*end == ')') return end+(start==end);

      // Flow control characters that end pipeline segments
      s = end + anystart(end, (char *[]){";;&", ";;", ";&", ";", "||",
        "|&", "|", "&&", "&", "(", ")", 0});
      if (s != end) return (end == start) ? s : end;
    }

    // Things the same unquoted or in most non-single-quote contexts

    // start new quote context?
    if (strchr("\"'`", *end)) toybuf[quote++] = *end++;

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

// TODO: should this be inlined into type 1 processing to set blk->end and
// then everything else use that?

  while (pl) {
    if (pl->type == 1 || pl->type == 'f') i++;
    else if (pl->type == 3) if (--i<1) break;
    pl = pl->next;
  }

  return pl;
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
        array_add(&arg->v, arg->c++, xstrdup(line));
        array_add(&arg->v, arg->c, arg->v[arg->c]);
        arg->c++;
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
    while (isspace(*start)) ++start;
    if (*start=='#') while (*start && *start != '\n') ++start;

    // Parse next word and detect overflow (too many nested quotes).
    if ((end = parse_word(start)) == (void *)1)
      goto flush;

    // Is this a new pipeline segment?
    if (!pl) {
      pl = xzalloc(sizeof(struct sh_pipeline));
      arg = pl->arg;
      dlist_add_nomalloc((void *)&sp->pipeline, (void *)pl);
    }

    // Do we need to request another line to finish word (find ending quote)?
    if (!end) {
      // Save unparsed bit of this line, we'll need to re-parse it.
      array_add(&arg->v, arg->c++, xstrndup(start, strlen(start)));
      arg->c = -arg->c;
      free(delete);

      return 1;
    }

    // Ok, we have a word. What does it _mean_?

    // Did we hit end of line or ) outside a function declaration?
    // ) is only saved at start of a statement, ends current statement
    if (end == start || (arg->c && *start == ')' && pl->type!='f')) {
      if (!arg->v) array_add(&arg->v, arg->c, 0);

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
    array_add(&arg->v, arg->c, s = xstrndup(start, end-start));
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
      dprintf(255, "expecting %s\n", dl->data);
    if (sp->pipeline)
      dprintf(255, "pipeline count=%d here=%d\n", sp->pipeline->prev->count,
        sp->pipeline->prev->here);
  }

  for (pl = sp->pipeline; pl ; pl = (pl->next == sp->pipeline) ? 0 : pl->next) {
    for (i = 0; i<pl->arg->c; i++)
      dprintf(255, "arg[%d][%ld]=%s\n", q, i, pl->arg->v[i]);
    if (pl->arg->c<0) dprintf(255, "argc=%d\n", pl->arg->c);
    else dprintf(255, "type=%d term[%d]=%s\n", pl->type, q++, pl->arg->v[pl->arg->c]);
  }
}

void dump_filehandles(char *when)
{
  int fd = open("/proc/self/fd", O_RDONLY);
  DIR *dir = fdopendir(fd);
  char buf[256];

  if (dir) {
    struct dirent *dd;

    while ((dd = readdir(dir))) {
      if (atoi(dd->d_name)!=fd && 0<readlinkat(fd, dd->d_name, buf,sizeof(buf)))
        dprintf(2, "OPEN %s %d: %s = %s\n", when, getpid(), dd->d_name, buf);
    }
    closedir(dir);
  }
  close(fd);
}

/* Flow control statements:

  if/then/elif/else/fi, for select while until/do/done, case/esac,
  {/}, [[/]], (/), function assignment
*/


// wait for every process in a pipeline to end
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

// pipe data into and out of this segment, I.E. handle leading and trailing |
static int pipe_segments(char *ctl, int *pipes, int **urd)
{
  unredirect(*urd);
  *urd = 0;

  // Did the previous pipe segment pipe input into us?
  if (*pipes != -1) {
    save_redirect(urd, *pipes, 0);
    close(*pipes);
    *pipes = -1;
  }

  // are we piping output to the next segment?
  if (ctl && *ctl == '|' && ctl[1] != '|') {
    if (pipe(pipes)) {
      perror_msg("pipe");
// TODO record pipeline rc
// TODO check did not reach end of pipeline after loop
      return 1;
    }
    if (pipes[1] != 1) {
      save_redirect(urd, pipes[1], 1);
      close(pipes[1]);
    }
    fcntl(*pipes, F_SETFD, FD_CLOEXEC);
  }

  return 0;
}

// Handle && and || traversal in pipeline segments
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
static void run_function(struct sh_pipeline *pl)
{
  struct sh_pipeline *end;
  struct blockstack {
    struct blockstack *next;
    struct sh_pipeline *start, *end;
    struct sh_process *pin;      // processes piping into this block
    int run, loop, *urd, pout;
    struct sh_arg farg;          // for/select arg stack
    struct arg_list *fdelete; // farg's cleanup list
    char *fvar;                  // for/select's iteration variable name
  } *blk = 0, *new;
  struct sh_process *pplist = 0; // processes piping into current level
  int *urd = 0, pipes[2] = {-1, -1};
  long i;

// TODO can't free sh_process delete until ready to dispose else no debug output

  // iterate through pipeline segments
  while (pl) {
    struct sh_arg *arg = pl->arg;
    char *s = *arg->v, *ss = arg->v[1], *ctl = arg->v[arg->c];
if (BUGBUG) dprintf(255, "%d runtype=%d %s %s\n", getpid(), pl->type, s, ctl);
    // Is this an executable segment?
    if (!pl->type) {

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
        if (!blk || arg->c>2 || ss[strspn(ss, "0123456789")]) {
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

          // if ending a block, free, cleanup redirects and pop stack.
          llist_traverse(blk->fdelete, free);
          unredirect(blk->urd);
          if (*pipes) close(*pipes);
          *pipes = blk->pout;
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
//       I.E. run_subshell() here sometimes? (But when?)

        dlist_add_nomalloc((void *)&pplist, (void *)run_command(arg));
      }

      if (*pipes == -1) {
        toys.exitval = wait_pipeline(pplist);
        llist_traverse(pplist, free_process);
        pplist = 0;
        pl = skip_andor(toys.exitval, pl);
      }

    // Start of flow control block?
    } else if (pl->type == 1) {
      struct sh_process *pp = 0;

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
        if (pipe_segments(end->arg->v[end->arg->c], pipes, &urd)) break;

        // It's a new block we're running, save context and add it to the stack.
        new = xzalloc(sizeof(*blk));
        new->next = blk;
        blk = new;
        blk->start = pl;
        blk->end = end;
        blk->run = 1;

        // save context until block end
        blk->pout = *pipes;
        blk->urd = urd;
        urd = 0;
        *pipes = -1;

        // Perform redirects listed at end of block
        pp = expand_redir(end->arg, 1, blk->urd);
        blk->urd = pp->urd;
        if (pp->arg.c) {
// TODO this is a syntax_error
          perror_msg("unexpected %s", *pp->arg.v);
          llist_traverse(pp->delete, free);
          free(pp);
          break;
        }
      }

      // What flow control statement is this?

      // {/} if/then/elif/else/fi, while until/do/done - no special handling

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

// TODO case/esac [[/]] (/) ((/)) function/}

/*
TODO: a | b | c needs subshell for builtins?
        - anything that can produce output
        - echo declare dirs
      (a; b; c) like { } but subshell
      when to auto-exec? ps vs sh -c 'ps' vs sh -c '(ps)'
*/

      // subshell
      } else if (!strcmp(s, "(")) {
        if (!CFG_TOYBOX_FORK) {
          ss = pl2str(pl->next);
          pp->pid = run_subshell(ss, strlen(ss));
          free(ss);
        } else {
          if (!(pp->pid = fork())) {
            run_function(pl->next);
            _exit(toys.exitval);
          }
        }

        dlist_add_nomalloc((void *)&pplist, (void *)pp);
        pl = blk->end->prev;
      }

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

      // if we end a block we're not in, we started in a block.
      if (!blk) break;

      // repeating block?
      if (blk->run && !strcmp(s, "done")) {
        pl = blk->start;
        continue;
      }

// TODO goto "break" above instead of copying it here?
      // if ending a block, free, cleanup redirects, and pop stack.
      // needing to unredirect(urd) or close(pipes[0]) here would be syntax err
      llist_traverse(blk->fdelete, free);
      unredirect(blk->urd);
      *pipes = blk->pout;
      free(llist_pop(&blk));
    } else if (pl->type == 'f') pl = add_function(s, pl);

    pl = pl->next;
  }

  // did we exit with unfinished stuff?
  if (*pipes != -1) close(*pipes);
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

// Parse and run a self-contained command line with no prompt/continuation
static int sh_run(char *new)
{
  struct sh_function scratch;
  int rc;

// TODO switch the fmemopen for -c to use this? Error checking? $(blah)

  memset(&scratch, 0, sizeof(struct sh_function));
  if (!parse_line(new, &scratch)) run_function(scratch.pipeline);
  free_function(&scratch);
  rc = toys.exitval;
  toys.exitval = 0;

  return rc;
}

// Print prompt to stderr, parsing escapes
// Truncated to 4k at the moment, waiting for somebody to complain.
static void do_prompt(char *prompt)
{
  char *s, *ss, c, cc, *pp = toybuf;
  int len, ll;

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
      } else if (cc=='w') {
        if ((s = getvar("PWD"))) {
          if ((ss = getvar("HOME")) && strstart(&s, ss)) {
            *pp++ = '~';
            if (--len && *s!='/') *pp++ = '/';
            len--;
          }
          if (len>0) {
            ll = strlen(s);
            pp = stpncpy(pp, s, ll>len ? len : ll);
          }
        }
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

// only set local variable when global not present
static void setonlylocal(char ***to, char *name, char *val)
{
  if (getenv(name)) return;
  *(*to)++ = xmprintf("%s=%s", name, val ? val : "");
}

// init locals, sanitize environment, handle nommu subshell handoff
void subshell_setup(void)
{
  struct passwd *pw = getpwuid(getuid());
  int to, from, pid = 0, ppid = 0, mypid, myppid, len;
  char *s, *ss, **ll, *locals[] = {"GROUPS=", "SECONDS=", "RANDOM=", "LINENO=",
    xmprintf("PPID=%d", myppid = getppid()), xmprintf("EUID=%d", geteuid()),
    xmprintf("$=%d", mypid = getpid()), xmprintf("UID=%d", getuid())};
  struct stat st;
  struct utsname uu;
  FILE *fp;

  // Initialize read only local variables
  TT.locals = xmalloc(32*sizeof(char *));
  memcpy(TT.locals, locals, sizeof(locals));
  ll = TT.locals+(TT.loc_ro = ARRAY_LEN(locals));
  TT.loc_magic = 4;

  // Add local variables that can be overwritten
  setonlylocal(&ll, "PATH", _PATH_DEFPATH);
  if (!pw) pw = (void *)toybuf; // first use, so still zeroed
  setonlylocal(&ll, "HOME", *pw->pw_dir ? pw->pw_dir : "/");
  setonlylocal(&ll, "SHELL", pw->pw_shell);
  setonlylocal(&ll, "USER", pw->pw_name);
  setonlylocal(&ll, "LOGNAME", pw->pw_name);
  gethostname(toybuf, sizeof(toybuf)-1);
  *ll++ = xmprintf("HOSTNAME=%s", toybuf);
  uname(&uu);
  setonlylocal(&ll, "HOSTTYPE", uu.machine);
  sprintf(toybuf, "%s-unknown-linux", uu.machine);
  setonlylocal(&ll, "MACHTYPE", toybuf);
  setonlylocal(&ll, "OSTYPE", uu.sysname);
  // sprintf(toybuf, "%s-toybox", TOYBOX_VERSION);
  // setonlylocal(&ll, "BASH_VERSION", toybuf);
  *ll++ = xstrdup("OPTERR=1");
  *toybuf = 0;
  if (readlink0("/proc/self/exe", toybuf, sizeof(toybuf)))
    setonlylocal(&ll, "BASH", toybuf);
  *ll = 0;

  // Ensure environ copied and toys.envc set, and clean out illegal entries
  xunsetenv("");
  for (to = from = 0; (s = environ[from]); from++) {

    // If nommu subshell gets handoff
    if (!CFG_TOYBOX_FORK && !toys.stacktop) {
      len = 0;
      sscanf(s, "@%d,%d%n", &pid, &ppid, &len);
      if (len && s[len]) pid = ppid = 0;
    }

    // Filter out non-shell variable names
    for (len = 0; s[len] && ((s[len] == '_') || !ispunct(s[len])); len++);
    if (s[len] == '=') environ[to++] = environ[from];
  }
  environ[toys.optc = to] = 0;

  // set/update PWD
  sh_run("cd .");

  // set _ to path to this shell
  s = toys.argv[0];
  ss = 0;
  if (!strchr(s, '/')) {
    if (!(ss = getcwd(0, 0))) {
      if (*toybuf) s = toybuf;
    } else {
      s = xmprintf("%s/%s", ss, s);
      free(ss);
      ss = s;
    }
  }
  xsetenv("_", s);
  free(ss);
  if (!getvar("SHLVL")) xsetenv("SHLVL", "1");

//TODO indexed array,associative array,integer,local,nameref,readonly,uppercase
//          if (s+1<ss && strchr("aAilnru", *s)) {

  // sanity check: magic env variable, pipe status
  if (CFG_TOYBOX_FORK || toys.stacktop || pid!=mypid || ppid!=myppid) return;
  if (fstat(254, &st) || !S_ISFIFO(st.st_mode)) error_exit(0);
  fcntl(254, F_SETFD, FD_CLOEXEC);
  fp = fdopen(254, "r");

  // This is not efficient, could array_add the local vars.
// TODO implicit exec when possible
  while ((s = xgetline(fp, 0))) to = sh_run(s);
  fclose(fp);

  toys.exitval = to;
  xexit();
}

void sh_main(void)
{
  FILE *f;
  char *new;
  struct sh_function scratch;
  int prompt = 0;

  TT.hfd = 10;
  signal(SIGPIPE, SIG_IGN);

  // TODO euid stuff?
  // TODO login shell?
  // TODO read profile, read rc

  // if (!FLAG(noprofile)) { }

if (BUGBUG) { int fd = open("/dev/tty", O_RDWR); dup2(fd, 255); close(fd); }
  // Is this an interactive shell?
//  if (FLAG(i) || (!FLAG(c)&&(FLAG(S)||!toys.optc) && isatty(0) && isatty(1))) 

  // Set up signal handlers and grab control of this tty.

  // Read environment for exports from parent shell
  subshell_setup();

  memset(&scratch, 0, sizeof(scratch));

// TODO unify fmemopen() here with sh_run
  if (TT.c) f = fmemopen(TT.c, strlen(TT.c), "r");
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
// TODO line editing/history, should set $COLUMNS $LINES and sigwinch update
    if (!(new = xgetline(f ? f : stdin, 0))) break;
// TODO if (!isspace(*new)) add_to_history(line);

    // returns 0 if line consumed, command if it needs more data
    prompt = parse_line(new, &scratch);
if (BUGBUG) dump_state(&scratch);
    if (prompt != 1) {
// TODO: ./blah.sh one two three: put one two three in scratch.arg
      if (!prompt) run_function(scratch.pipeline);
      free_function(&scratch);
      prompt = 0;
    }
    free(new);
  }

  if (prompt) error_exit("%ld:unfinished line"+4*!TT.lineno, TT.lineno);
  toys.exitval = f && ferror(f);
  clearerr(stdout);
}

/********************* shell builtin functions *************************/

#define CLEANUP_sh
#define FOR_cd
#include "generated/flags.h"
void cd_main(void)
{
  char *home = getvar("HOME"), *pwd = getvar("PWD"), *dd = 0, *from, *to,
    *dest = (*toys.optargs && **toys.optargs) ? *toys.optargs : "~";
  int bad = 0;

  // TODO: CDPATH? Really?

  if (!home) home = "/";

  // expand variables
  if (dest) dd = expand_one_arg(dest, FORCE_COPY|NO_SPLIT, 0);
  if (!dd || !*dd) {
    free(dd);
    dd = xstrdup("/");
  }

  // prepend cwd or $PWD to relative path
  if (*dd != '/') {
    to = 0;
    from = pwd ? pwd : (to = getcwd(0, 0));
    if (!from) xsetenv("PWD", "(nowhere)");
    else {
      from = xmprintf("%s/%s", from, dd);
      free(dd);
      free(to);
      dd = from;
    }
  }

  if (FLAG(P)) {
    struct stat st;
    char *pp;

    // Does this directory exist?
    if ((pp = xabspath(dd, 1)) && stat(pp, &st) && !S_ISDIR(st.st_mode))
      bad++, errno = ENOTDIR;
    else {
      free(dd);
      dd = pp;
    }
  } else {

    // cancel out . and .. in the string
    for (from = to = dd; *from;) {
      if (*from=='/' && from[1]=='/') from++;
      else if (*from!='/' || from[1]!='.') *to++ = *from++;
      else if (!from[2] || from[2]=='/') from += 2;
      else if (from[2]=='.' && (!from[3] || from[3]=='/')) {
        from += 3;
        while (to>dd && *--to != '/');
      } else *to++ = *from++;
    }
    if (to == dd) to++;
    if (to-dd>1 && to[-1]=='/') to--;
    *to = 0;
  }

  if (bad || chdir(dd)) perror_msg("chdir '%s'", dd);
  else {
    if (pwd) xsetenv("OLDPWD", pwd);
    xsetenv("PWD", dd);
  }
  free(dd);
}

void exit_main(void)
{
  exit(*toys.optargs ? atoi(*toys.optargs) : 0);
}
