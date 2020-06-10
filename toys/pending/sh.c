/* sh.c - toybox shell
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * This shell aims for bash compatibility. The bash man page is at:
 * http://man7.org/linux/man-pages/man1/bash.1.html
 *
 * The POSIX-2008/SUSv4 shell spec is at:
 * http://opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
 * and http://opengroup.org/onlinepubs/9699919799/utilities/sh.html
 *
 * The chap02 link describes the following shell builtins:
 *
 *   break : continue exit
 *   . eval exec export readonly return set shift times trap unset
 *
 * The second link (the utilities directory) also contains specs for the
 * following shell builtins:
 *
 *   cd ulimit umask
 *   alias bg command fc fg getopts hash jobs kill read type unalias wait
 *
 * deviations from posix: don't care about $LANG or $LC_ALL

 * TODO: test that $PS1 color changes work without stupid \[ \] hack
 * TODO: Handle embedded NUL bytes in the command line? (When/how?)

 * builtins: alias bg command fc fg getopts jobs newgrp read umask unalias wait
 *           disown umask suspend source pushd popd dirs logout times trap
 *           unset local export readonly set : . let history declare
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
 *
 * Flow control statements:
 *
 * if/then/elif/else/fi, for select while until/do/done, case/esac,
 * {/}, [[/]], (/), function assignment

USE_SH(NEWTOY(cd, ">1LP[-LP]", TOYFLAG_NOFORK))
USE_SH(NEWTOY(eval, 0, TOYFLAG_NOFORK))
USE_SH(NEWTOY(exec, "^cla:", TOYFLAG_NOFORK))
USE_SH(NEWTOY(exit, 0, TOYFLAG_NOFORK))
USE_SH(NEWTOY(export, "np", TOYFLAG_NOFORK))
USE_SH(NEWTOY(shift, ">1", TOYFLAG_NOFORK))
USE_SH(NEWTOY(unset, "fvn", TOYFLAG_NOFORK))

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

config UNSET
  bool
  default n
  depends on SH
  help
    usage: unset [-fvn] NAME...

    -f	NAME is a function
    -v	NAME is a variable
    -n	dereference NAME and unset that

config EVAL
  bool
  default n
  depends on SH
  help
    usage: eval COMMAND...

    Execute (combined) arguments as a shell command.

config EXEC
  bool
  default n
  depends on SH
  help
    usage: exec [-cl] [-a NAME] COMMAND...

    -a	set argv[0] to NAME
    -c	clear environment
    -l	prepend - to argv[0]

config EXPORT
  bool
  default n
  depends on SH
  help
    usage: export [-n] [NAME[=VALUE]...]

    Make variables available to child processes. NAME exports existing local
    variable(s), NAME=VALUE sets and exports.

    -n	Unexport. Turn listed variable(s) into local variables.

    With no arguments list exported variables/attributes as "declare" statements.

config JOBS
  bool
  default n
  depends on SH
  help
    usage: jobs [-lnprs] [%JOB | -x COMMAND...]

    List running/stopped background jobs.

    -l Include process ID in list
    -n Show only new/changed processes
    -p Show process IDs only
    -r Show running processes
    -s Show stopped processes

config SHIFT
  bool
  default n
  depends on SH
  help
    usage: shift [N]

    Skip N (default 1) positional parameters, moving $1 and friends along the list.
    Does not affect $0.
*/

#define FOR_sh
#include "toys.h"

GLOBALS(
  union {
    struct {
      char *c;
    } sh;
    struct {
      char *a;
    } exec;
  };

  // keep lineno here, we use it to work around a compiler limitation
  long lineno;
  char *ifs, *isexec;
  unsigned options, jobcnt;
  int hfd, pid, bangpid, varslen, shift, cdcount;
  long long SECONDS;

  struct sh_vars {
    long flags;
    char *str;
  } *vars;

  // Parsed function
  struct sh_function {
    char *name;
    struct sh_pipeline {  // pipeline segments
      struct sh_pipeline *next, *prev, *end;
      int count, here, type; // TODO abuse type to replace count during parsing
      struct sh_arg {
        char **v;
        int c;
      } arg[1];
    } *pipeline;
    struct double_list *expect; // should be zero at end of parsing
  } *functions;

// TODO ctrl-Z suspend should stop script
  struct sh_process {
    struct sh_process *next, *prev; // | && ||
    struct arg_list *delete;   // expanded strings
    // undo redirects, a=b at start, child PID, exit status, has !, job #
    int *urd, envlen, pid, exit, not, job;
    long long when; // when job backgrounded/suspended
// TODO struct sh_arg *raw;  // for display
    struct sh_arg arg;
  } *pp; // currently running process

  struct sh_arg jobs, *arg;  // job list, command line args for $* etc
)

// Can't yet avoid this prototype. Fundamental problem is $($($(blah))) nests,
// leading to function loop with run->parse->run
static int sh_run(char *new);

#define BUGBUG 0

// call with NULL to just dump FDs
static void dump_state(struct sh_function *sp)
{
  struct sh_pipeline *pl;
  long i;
  int q = 0, fd = open("/proc/self/fd", O_RDONLY);
  DIR *dir = fdopendir(fd);
  char buf[256];

  if (sp && sp->expect) {
    struct double_list *dl;

    for (dl = sp->expect; dl; dl = (dl->next == sp->expect) ? 0 : dl->next)
      dprintf(255, "expecting %s\n", dl->data);
    if (sp->pipeline)
      dprintf(255, "pipeline count=%d here=%d\n", sp->pipeline->prev->count,
        sp->pipeline->prev->here);
  }

  if (sp) for (pl = sp->pipeline; pl ; pl = (pl->next == sp->pipeline) ? 0 : pl->next) {
    for (i = 0; i<pl->arg->c; i++)
      dprintf(255, "arg[%d][%ld]=%s\n", q, i, pl->arg->v[i]);
    if (pl->arg->c<0) dprintf(255, "argc=%d\n", pl->arg->c);
    else dprintf(255, "type=%d term[%d]=%s\n", pl->type, q++, pl->arg->v[pl->arg->c]);
  }

  if (dir) {
    struct dirent *dd;

    while ((dd = readdir(dir))) {
      if (atoi(dd->d_name)!=fd && 0<readlinkat(fd, dd->d_name, buf,sizeof(buf)))
        dprintf(255, "OPEN %d: %s = %s\n", getpid(), dd->d_name, buf);
    }
    closedir(dir);
  }
  close(fd);
}

// ordered for greedy matching, so >&; becomes >& ; not > &;
// making these const means I need to typecast the const away later to
// avoid endless warnings.
static const char *redirectors[] = {"<<<", "<<-", "<<", "<&", "<>", "<", ">>",
  ">&", ">|", ">", "&>>", "&>", 0};

#define OPT_I           1
#define OPT_BRACE       2   // set -B
#define OPT_NOCLOBBER   4   // set -C
#define OPT_S           8
#define OPT_C          16

static void syntax_err(char *s)
{
  error_msg("syntax error: %s", s);
  toys.exitval = 2;
}

// append to array with null terminator and realloc as necessary
static void arg_add(struct sh_arg *arg, char *data)
{
  if (!(arg->c&31)) arg->v = xrealloc(arg->v, sizeof(char *)*(arg->c+33));
  arg->v[arg->c++] = data;
  arg->v[arg->c] = 0;
}

// add argument to an arg_list
static void push_arg(struct arg_list **list, char *arg)
{
  struct arg_list *al;

  if (!list) return;
  al = xmalloc(sizeof(struct arg_list));
  al->next = *list;
  al->arg = arg;
  *list = al;
}

static void arg_add_del(struct sh_arg *arg, char *data,struct arg_list **delete)
{
  push_arg(delete, data);
  arg_add(arg, data);
}

// return length of valid variable name
static char *varend(char *s)
{
  if (isdigit(*s)) return s;
  while (*s>' ' && (*s=='_' || !ispunct(*s))) s++;

  return s;
}

// Return index of variable within this list
static struct sh_vars *findvar(char *name)
{
  int len = varend(name)-name;
  struct sh_vars *var = TT.vars+TT.varslen;

  if (len) while (var-- != TT.vars) 
    if (!strncmp(var->str, name, len) && var->str[len] == '=') return var;

  return 0;
}

// Append variable to TT.vars, returning *struct. Does not check duplicates.
static struct sh_vars *addvar(char *s)
{
  if (!(TT.varslen&31))
    TT.vars = xrealloc(TT.vars, (TT.varslen+32)*sizeof(*TT.vars));
  TT.vars[TT.varslen].flags = 0;
  TT.vars[TT.varslen].str = s;

  return TT.vars+TT.varslen++;
}

// TODO function to resolve a string into a number for $((1+2)) etc
long long do_math(char *s)
{
  return atoll(s);
}

// Assign one variable from malloced key=val string, returns var struct
// TODO implement remaining types
#define VAR_DICT      256
#define VAR_ARRAY     128
#define VAR_INT       64
#define VAR_TOLOWER   32
#define VAR_TOUPPER   16
#define VAR_NAMEREF   8
#define VAR_GLOBAL    4
#define VAR_READONLY  2
#define VAR_MAGIC     1

// declare -aAilnrux
// ft
static struct sh_vars *setvar(char *s)
{
  int len = varend(s)-s;
  long flags;
  struct sh_vars *var;

  if (s[len] != '=') {
    error_msg("bad setvar %s\n", s);
    free(s);
    return 0;
  }
  if (len == 3 && !memcmp(s, "IFS", 3)) TT.ifs = s+4;

  if (!(var = findvar(s))) return addvar(s);
  flags = var->flags;

  if (flags&VAR_READONLY) {
    error_msg("%.*s: read only", len, s);
    free(s);

    return 0;
  } else if (flags&VAR_MAGIC) {
    if (*s == 'S') TT.SECONDS = millitime() - 1000*do_math(s+len-1);
    else if (*s == 'R') srandom(do_math(s+len-1));
  } else if (flags&VAR_GLOBAL) xsetenv(var->str = s, 0);
  else {
    free(var->str);
    var->str = s;
  }
// TODO if (flags&(VAR_TOUPPER|VAR_TOLOWER)) 
// unicode _is stupid enough for upper/lower case to be different utf8 byte
// lengths. example: lowercase of U+0130 (C4 B0) is U+0069 (69)
// TODO VAR_INT
// TODO VAR_ARRAY VAR_DICT

  return var;
}

static void unsetvar(char *name)
{
  struct sh_vars *var = findvar(name);
  int ii = var-TT.vars;

  if (!var) return;
  if (var->flags&VAR_GLOBAL) xunsetenv(name);
  else free(var->str);

  memmove(TT.vars+ii, TT.vars+ii+1, TT.varslen-ii);
  TT.varslen--;
}

static struct sh_vars *setvarval(char *name, char *val)
{
  return setvar(xmprintf("%s=%s", name, val));
}

// get value of variable starting at s.
static char *getvar(char *s)
{
  struct sh_vars *var = findvar(s);

  if (!var) return 0;

  if (var->flags & VAR_MAGIC) {
    char c = *var->str;

    if (c == 'S') sprintf(toybuf, "%lld", (millitime()-TT.SECONDS)/1000);
    else if (c == 'R') sprintf(toybuf, "%ld", random()&((1<<16)-1));
    else if (c == 'L') sprintf(toybuf, "%ld", TT.lineno);
    else if (c == 'G') sprintf(toybuf, "TODO: GROUPS");

    return toybuf;
  }

  return varend(var->str)+1;
}

// malloc declare -x "escaped string"
static char *declarep(struct sh_vars *var)
{
  char *types = "-rgnuliaA", *in = types, flags[16], *out = flags, *ss;
  int len;

  while (*++in) if (var->flags&(1<<(in-types))) *out++ = *in;
  if (in == types) *out++ = *types;
  *out = 0;
  len = out-flags;

  for (in = types = varend(var->str); *in; in++) len += !!strchr("$\"\\`", *in);
  len += in-types;
  ss = xmalloc(len+13);

  out = ss + sprintf(ss, "declare -%s \"", out);
  while (types) {
    if (strchr("$\"\\`", *in)) *out++ = '\\';
    *out++ = *types++;
  }
  *out++ = '"';
  *out = 0;
 
  return ss; 
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
    if (*(s = varend(s+1)) == '}' && s != word+1) s++;
    else s = word;
  } else while (isdigit(*s)) s++;

  return s-word;
}

// parse next word from command line. Returns end, or 0 if need continuation
// caller eats leading spaces. early = skip one quote block (or return start)
static char *parse_word(char *start, int early)
{
  int i, quote = 0, q, qc = 0;
  char *end = start, *s;

  // Things we should only return at the _start_ of a word

  if (strstart(&end, "<(") || strstart(&end, ">(")) toybuf[quote++]=')';

  // Redirections. 123<<file- parses as 2 args: "123<<" "file-".
  s = end + redir_prefix(end);
  if ((i = anystart(s, (void *)redirectors))) return s+i;

  // (( is a special quote at the start of a word
  if (strstart(&end, "((")) toybuf[quote++] = 254;

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
      if ((q == ')' || q >= 254) && (*end == '(' || *end == ')')) {
        if (*end == '(') qc++;
        else if (qc) qc--;
        else if (q >= 254) {
          // (( can end with )) or retroactively become two (( if we hit one )
          if (strstart(&end, "))")) quote--;
          else if (q == 254) return start+1;
          else if (q == 255) toybuf[quote-1] = ')';
        } else if (*end == ')') quote--;
        end++;

      // end quote?
      } else if (*end == q) quote--, end++;

      // single quote claims everything
      else if (q == '\'') end++;
      else i++;

      // loop if we already handled a symbol and aren't stopping early
      if (early && !quote) return end;
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
    if (strchr("\"'`", *end)) toybuf[quote++] = *end;

    // backslash escapes
    else if (*end == '\\') {
      if (!end[1] || (end[1]=='\n' && !end[2])) return 0;
      end += 2;
    } else if (*end == '$' && -1 != (i = stridx("({[", end[1]))) {
      end++;
      if (strstart(&end, "((")) toybuf[quote++] = 255;
      else {
        toybuf[quote++] = ")}]"[i];
        end++;
      }
    }
    if (early && !quote) return end;
    end++;
  }

  return quote ? 0 : end;
}

// Return next available high (>=10) file descriptor
static int next_hfd()
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
// If from >= 0 dup from->to after saving to. If from == -1 just save to.
// if from == -2 schedule "to" to be closed by unredirect.
static int save_redirect(int **rd, int from, int to)
{
  int cnt, hfd, *rr;

  if (from == to) return 0;
  // save displaced to, copying to high (>=10) file descriptor to undo later
  // except if we're saving to environment variable instead (don't undo that)
  if (from>-2) {
    if ((hfd = next_hfd())==-1) return 1;
    if (hfd != dup2(to, hfd)) hfd = -1;
    else fcntl(hfd, F_SETFD, FD_CLOEXEC);
if (BUGBUG) dprintf(255, "%d redir from=%d to=%d hfd=%d\n", getpid(), from, to, hfd);
    // dup "to"
    if (from >= 0 && to != dup2(from, to)) {
      if (hfd >= 0) close(hfd);

      return 1;
    }
  } else {
if (BUGBUG) dprintf(255, "%d schedule close %d\n", getpid(), to);
    hfd = to;
    to = -1;
  }

  // Append undo information to redirect list so we can restore saved hfd later.
  if (!((cnt = *rd ? **rd : 0)&31)) *rd = xrealloc(*rd, (cnt+33)*2*sizeof(int));
  *(rr = *rd) = ++cnt;
  rr[2*cnt-1] = hfd;
  rr[2*cnt] = to;

  return 0;
}

// TODO: waitpid(WNOHANG) to clean up zombies and catch background& ending
static void subshell_callback(char **argv)
{
  char *s;

  xsetenv(s = xmprintf("@%d,%d=", getpid(), getppid()), 0);
  s[strlen(s)-1] = 0;
  xsetenv(xmprintf("$=%d", TT.pid), 0);
// TODO: test $$ in (nommu)
}

// TODO check every caller of run_subshell for error, or syntax_error() here
// from pipe() failure

// Pass environment and command string to child shell, return PID of child
static int run_subshell(char *str, int len)
{
  pid_t pid;

if (BUGBUG) dprintf(255, "run_subshell %.*s\n", len, str);
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
    int pipes[2], i, c;

    // open pipe to child
    if (pipe(pipes) || 254 != dup2(pipes[0], 254)) return 1;
    close(pipes[0]);
    fcntl(pipes[1], F_SETFD, FD_CLOEXEC);

    // vfork child
    pid = xpopen_setup(0, 0, subshell_callback);

    // free entries added to end of environment by callback (shared heap)
    for (i = 0; environ[i]; i++) {
      c = environ[i][0];
      if (c == '_' || !ispunct(c)) continue;
      free(environ[i]);
      environ[i] = 0;
    }

    // marshall data to child
    close(254);
    for (i = 0; i<TT.varslen; i++) {
      char *s;

      if (TT.vars[i].flags&VAR_GLOBAL) continue;
      dprintf(pipes[1], "%s\n", s = declarep(TT.vars+i));
      free(s);
    }
    dprintf(pipes[1], "%.*s\n", len, str);
    close(pipes[1]);
  }

  return pid;
}

// restore displaced filehandles, closing high filehandles they were copied to
static void unredirect(int *urd)
{
  int *rr = urd+1, i;

  if (!urd) return;

  for (i = 0; i<*urd; i++, rr += 2) {
if (BUGBUG) dprintf(255, "%d urd %d %d\n", getpid(), rr[0], rr[1]);
    if (rr[0] != -1) {
      // No idea what to do about fd exhaustion here, so Steinbach's Guideline.
      dup2(rr[0], rr[1]);
      close(rr[0]);
    }
  }
  free(urd);
}

// Call subshell with either stdin/stdout redirected, return other end of pipe
static int pipe_subshell(char *s, int len, int out)
{
  int pipes[2], *uu = 0, in = !out;

  // Grab subshell data
  if (pipe(pipes)) {
    perror_msg("%.*s", len, s);

    return -1;
  }

  // Perform input or output redirect and launch process (ignoring errors)
  save_redirect(&uu, pipes[in], in);
  close(pipes[in]);
  run_subshell(s, len);
  unredirect(uu);

  return pipes[out];
}

// utf8 strchr: return wide char matched at wc from chrs, or 0 if not matched
// if len, save length of wc
static int utf8chr(char *wc, char *chrs, int *len)
{
  wchar_t wc1, wc2;
  int ll;

  if (len) *len = 1;
  if (!*wc) return 0;
  if (0<(ll = utf8towc(&wc1, wc, 99))) {
    if (len) *len = ll;
    while (*chrs) {
      if(1>(ll = utf8towc(&wc2, chrs, 99))) chrs++;
      else {
        if (wc1 == wc2) return wc1;
        chrs += ll;
      }
    }
  }

  return 0;
}

#define NO_PATH  (1<<0)    // path expansion (wildcards)
#define NO_SPLIT (1<<1)    // word splitting
#define NO_BRACE (1<<2)    // {brace,expansion}
#define NO_TILDE (1<<3)    // ~username/path
#define NO_QUOTE (1<<4)    // quote removal
#define SEMI_IFS (1<<5)    // Use ' ' instead of IFS to combine $*
// TODO: parameter/variable $(command) $((math)) split pathglob
// TODO: ${name:?error} causes an error/abort here (syntax_err longjmp?)
// TODO: $1 $@ $* need args marshalled down here: function+structure?
// arg = append to this
// str = string to expand
// flags = type of expansions (not) to do
// delete = append new allocations to this so they can be freed later
// TODO: at_args: $1 $2 $3 $* $@
static int expand_arg_nobrace(struct sh_arg *arg, char *str, unsigned flags,
  struct arg_list **delete)
{
  char cc, qq = 0, *old = str, *new = str, *s, *ss, *ifs, **aa;
  int ii = 0, dd, jj, kk, ll, oo = 0, nodel;

if (BUGBUG) dprintf(255, "expand %s\n", str);

  // Tilde expansion
  if (!(flags&NO_TILDE) && *str == '~') {
    struct passwd *pw = 0;

    ss = 0;
    while (str[ii] && str[ii]!=':' && str[ii]!='/') ii++;
    if (ii==1) {
      if (!(ss = getvar("HOME")) || !*ss) pw = bufgetpwuid(getuid());
    } else {
      // TODO bufgetpwnam
      pw = getpwnam(s = xstrndup(str+1, ii-1));
      free(s);
    }
    if (pw) {
      ss = pw->pw_dir;
      if (!ss || !*ss) ss = "/";
    }
    if (ss) {
      oo = strlen(ss);
      s = xmprintf("%s%s", ss, str+ii);
      if (old != new) free(new);
      new = s;
    }
  }

  // parameter/variable expansion, and dequoting

  for (; (cc = str[ii++]); old!=new && (new[oo] = 0)) {

    // skip literal chars
    if (!strchr("$'`\\\"", cc)) {
      if (old != new) new[oo++] = cc;
      continue;
    }

    // allocate snapshot if we just started modifying
    if (old == new) {
      new = xstrdup(new);
      new[oo = ii-1] = 0;
    }
    ifs = 0;
    aa = 0;
    nodel = 0;

    // handle different types of escapes
    if (cc == '\\') new[oo++] = str[ii] ? str[ii++] : cc;
    else if (cc == '"') qq++;
    else if (cc == '\'') {
      if (qq&1) new[oo++] = cc;
      else {
        qq += 2;
        while ((cc = str[ii++]) != '\'') new[oo++] = cc;
      }

    // both types of subshell work the same, so do $( here not in '$' below
// TODO $((echo hello) | cat) ala $(( becomes $( ( retroactively
    } else if (cc == '`' || (cc == '$' && strchr("([", str[ii]))) {
      off_t pp = 0;

      s = str+ii-1;
      kk = parse_word(s, 1)-s;
      if (str[ii] == '[' || *toybuf == 255) {
        s += 2+(str[ii]!='[');
        kk -= 3+2*(str[ii]!='[');
dprintf(2, "TODO: do math for %.*s\n", kk, s);
      } else {
        // Run subshell and trim trailing newlines
        s += (jj = 1+(cc == '$'));
        ii += --kk;
        kk -= jj;

        // Special case echo $(<input)
        for (ss = s; isspace(*ss); ss++);
        if (*ss != '<') ss = 0;
        else {
          while (isspace(*++ss));
          if (!(ll = parse_word(ss, 0)-ss)) ss = 0;
          else {
            jj = ll+(ss-s);
            while (isspace(s[jj])) jj++;
            if (jj != kk) ss = 0;
            else {
              jj = xcreate_stdio(ss = xstrndup(ss, ll), O_RDONLY|WARN_ONLY, 0);
              free(ss);
            }
          }
        }

// TODO what does \ in `` mean? What is echo `printf %s \$x` supposed to do?
        if (!ss) jj = pipe_subshell(s, kk, 0);
        if ((ifs = readfd(jj, 0, &pp)))
          for (kk = strlen(ifs); kk && ifs[kk-1]=='\n'; ifs[--kk] = 0);
        close(jj);
      }
    } else if (cc == '$') {

      // parse $ $'' ${} or $VAR

      cc = str[ii++];
      if (cc=='\'') {
        for (s = str+ii; *s != '\''; oo += wcrtomb(new+oo, unescape2(&s, 0),0));
        ii = s-str+1;

        continue;
      } else if (cc == '{') {
        cc = *(ss = str+ii);
        if (!(jj = strchr(ss, '}')-ss)) ifs = (void *)1;
        ii += jj+1;

        if (jj>1) {
          // handle ${!x} and ${#x}
          if (*ss == '!') {
            if (!(ss = getvar(ss+1)) || !*ss) continue;
            jj = varend(ss)-ss;
            if (ss[jj]) ifs = (void *)1;
          } else if (*ss == '#') {
            if (jj == 2 && (*ss == '@' || *ss == '*')) jj--;
            else ifs = xmprintf("%ld", (long)strlen(getvar(ss) ? : ""));
          }
        }
      } else {
        ss = str+--ii;
        if (!(jj = varend(ss)-ss)) jj++;
        ii += jj;
      }

// ${#nom} ${} ${x}
// ${x:-y} use default
// ${x:=y} assign default (error if positional)
// ${x:?y} err if null
// ${x:+y} alt value
// ${x:off} ${x:off:len} off<0 from end (must ": -"), len<0 also from end must
//   0-based indexing
// ${@:off:len} positional parameters, off -1 = len, -len is error
//   1-based indexing
// ${!x} deref (does bad substitution if name has : in it)
// ${!x*} ${!x@} names matching prefix
//   note: expands something other than arg->c
// ${x#y} remove shortest prefix ${x##y} remove longest prefix
//   x can be @ or *
// ${x%y} ${x%%y} suffix
// ${x/pat/sub} substitute ${x//pat/sub} global ${x/#pat/sub} begin
// ${x/%pat/sub} end ${x/pat} delete pat
//   x can be @ or *
// ${x^pat} ${x^^pat} uppercase/g ${x,} ${x,,} lowercase/g (no pat = ?)
// ${x@QEPAa} Q=$'blah' E=blah without the $'' wrap, P=expand as $PS1
//   A=declare that recreates var a=attribute flags
//   x can be @*

// TODO: $_ is last arg of last command, and exported as path to exe run
      if (ifs);
      else if (cc == '-') {
        s = ifs = xmalloc(8);
        if (TT.options&OPT_I) *s++ = 'i';
        if (TT.options&OPT_BRACE) *s++ = 'B';
        if (TT.options&OPT_S) *s++ = 's';
        if (TT.options&OPT_C) *s++ = 'c';
        *s = 0;
      } else if (cc == '?') ifs = xmprintf("%d", toys.exitval);
      else if (cc == '$') ifs = xmprintf("%d", TT.pid);
      else if (cc == '#') ifs = xmprintf("%d", TT.arg->c?TT.arg->c-1:0);
      else if (cc == '!') ifs = xmprintf("%d"+2*!TT.bangpid, TT.bangpid);
      else if (cc == '*' || cc == '@') aa = TT.arg->v+1;
      else if (isdigit(cc)) {
        for (kk = ll = 0; kk<jj && isdigit(ss[kk]); kk++)
          ll = (10*ll)+ss[kk]-'0';
        if (ll) ll += TT.shift;
        if (ll<TT.arg->c) ifs = TT.arg->v[ll];
        nodel = 1;

      // $VARIABLE
      } else {
        if (ss == varend(ss)) {
          ii--;
          if (ss[-1] == '$') new[oo++] = '$';
          else ifs = (void *)1;
        } else ifs = getvar(ss);
        nodel = 1;
      }
    }

// TODO: $((a=42)) can change var, affect lifetime
// must replace ifs AND any previous output arg[] within pointer strlen()
// TODO ${blah} here

    if (ifs == (void *)1) {
      error_msg("%.*s: bad substitution", (int)(s-(str+ii)+3), str+ii-2);
      free(new);

      return 1;
    }

    // combine before/ifs/after sections, splitting words on $IFS in ifs
    if (ifs || aa) {
      char sep[8];

      // If not gluing together, nothing to substitute, not quoted: do nothing
      if (!aa && !*ifs && !qq) continue;

      // Fetch separator
      *sep = 0;
      if ((qq&1) && cc=='*') {
        wchar_t wc;

        if (flags&SEMI_IFS) strcpy(sep, " ");
        else if (0<(dd = utf8towc(&wc, TT.ifs, 4)))
          sprintf(sep, "%.*s", dd, TT.ifs);
      }

      // when aa proceed through entries until NULL, else process ifs once
      do {

        // get next argument, is this last entry, find end of word
        if (aa) {
          ifs = *aa ? : "";
          if (*aa) aa++;
          nodel = 1;
        }
        if (qq&1) ss = ifs+strlen(ifs);
        else for (ss = ifs; *ss; ss += kk)
          if ((ll = utf8chr(ss, TT.ifs, &kk))) break;
        kk = !aa || !*aa;

        // loop within current ifs checking region to split words
        do {
          // fast path: use existing memory when no prefix, not splitting it,
          // and either not last entry or no suffix
          if (!oo && !*ss && (!kk || !str[ii]) && !((qq&1) && cc=='*')) {
            if (!qq && ss==ifs) break;
            arg_add_del(arg, ifs, nodel ? 0 : delete);
            nodel = 1;

            continue;
          }

          // resize allocation and copy next chunk of IFS-free data
          new = xrealloc(new, oo + (ss-ifs) + strlen(sep) +
                         ((jj = kk && !*ss) ? strlen(str+ii) : 0) + 1);
          oo += sprintf(new + oo, "%.*s", (int)(ss-ifs), ifs);
          if (!nodel) free(ifs);
          nodel = 1;
          if (jj) break;

          // for single quoted "$*" append IFS
          if ((qq&1) && cc == '*') oo += sprintf(new+oo, "%s", sep);

          // add argument if quoted, non-blank, or non-whitespace separator
          else {
            if (qq || *new || *ss) {
              arg_add_del(arg, new, nodel ? 0 : delete);
              nodel = 1;
            }
            qq &= 1;
            new = xstrdup(str+ii);
            oo = 0;
          }

          // Skip trailing seperator (combining whitespace)
          while ((jj = utf8chr(ss, TT.ifs, &ll)) && iswspace(jj)) ss += ll;

        } while (*(ifs = ss));
      } while (!kk);
    }
  }

// TODO globbing * ? [

// Word splitting completely eliminating argument when no non-$IFS data left
// wordexp keeps pattern when no matches

// TODO NO_SPLIT cares about IFS, see also trailing \n

// quote removal

  // Record result.
  if (*new || qq)
    arg_add_del(arg, new, (old != new) ? delete : 0);
  else if(old != new) free(new);

  return 0;
}

// expand braces (ala {a,b,c}) and call expand_arg_nobrace() each permutation
static int expand_arg(struct sh_arg *arg, char *old, unsigned flags,
  struct arg_list **delete)
{
  struct brace {
    struct brace *next, *prev, *stack;
    int active, cnt, idx, dots[2], commas[];
  } *bb = 0, *blist = 0, *bstk, *bnext;
  int i, j;
  char *s, *ss;

  // collect brace spans
  if ((TT.options&OPT_BRACE) && !(flags&NO_BRACE)) for (i = 0; ; i++) {
    while ((s = parse_word(old+i, 1)) != old+i) i += s-(old+i);
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

    // Save result, aborting on expand error
    push_arg(delete, ss);
    if (expand_arg_nobrace(arg, ss, flags, delete)) {
      llist_traverse(blist, free);

      return 1;
    }

    // increment
    for (bb = blist->prev; bb; bb = (bb == blist) ? 0 : bb->prev) {
      if (!bb->stack) continue;
      else if (++bb->idx > bb->cnt) bb->idx = 0;
      else break;
    }

    // if increment went off left edge, done expanding
    if (!bb) break;
  }
  llist_traverse(blist, free);

  return 0;
}

// Expand exactly one arg, returning NULL on error.
static char *expand_one_arg(char *new, unsigned flags, struct arg_list **del)
{
  struct sh_arg arg;
  char *s = 0;
  int i;

  memset(&arg, 0, sizeof(arg));
  if (!expand_arg(&arg, new, flags, del) && arg.c == 1) s = *arg.v;
  if (!del && !s) for (i = 0; i < arg.c; i++) free(arg.v[i]);
  free(arg.v);

  return s;
}

// TODO |&

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

    if (!strcmp(s, "!")) {
      pp->not ^= 1;

      continue;
    }

    // Handle <() >() redirectionss
    if ((*s == '<' || *s == '>') && s[1] == '(') {
      int new = pipe_subshell(s+2, strlen(s+2)-1, *s == '>');

      // Grab subshell data
      if (new == -1) {
        pp->exit = 1;

        return pp;
      }
      save_redirect(&pp->urd, -2, new);

      // bash uses /dev/fd/%d which requires /dev/fd to be a symlink to
      // /proc/self/fd so we just produce that directly.
      arg_add_del(&pp->arg, ss = xmprintf("/proc/self/fd/%d", new),&pp->delete);

      continue;
    }

    // Is this a redirect? s = prefix, ss = operator
    ss = s + redir_prefix(arg->v[j]);
    sss = ss + anystart(ss, (void *)redirectors);
    if (ss == sss) {
      // Nope: save/expand argument and loop
      if (expand_arg(&pp->arg, s, 0, &pp->delete)) {
        pp->exit = 1;

        return pp;
      }
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
      s = 0;
      break; // arg splitting here is an error
    }

    // Parse the [fd] part of [fd]<name
    to = *ss != '<';
    if (isdigit(*s)) to = atoi(s);
    else if (*s == '{') {
      if (*varend(s+1) != '}') break;
      // when we close a filehandle, we _read_ from {var}, not write to it
      if ((!strcmp(ss, "<&") || !strcmp(ss, ">&")) && !strcmp(sss, "-")) {
        if (!(ss = getvar(s+1))) break;
        to = atoi(ss); // TODO trailing garbage?
        if (save_redirect(&pp->urd, -1, to)) break;
        close(to);

        continue;
      // record high file descriptor in {to}<from environment variable
      } else {
        // we don't save this, it goes in the env var and user can close it.
        if (-1 == (to = next_hfd())) break;
        cv = xmprintf("%.*s=%d", (int)(ss-s-2), s+1, to);
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
          if (x && !(sss = expand_one_arg(sss, NO_PATH|NO_SPLIT, 0))) {
            s = 0;
            break;
          }
          len = strlen(sss);
          if (len != writeall(from, sss, len)) bad++;
          if (x) free(sss);
        } else {
          struct sh_arg *hh = arg+here++;

          for (i = 0; i<hh->c; i++) {
            ss = hh->v[i];
            sss = 0;
// TODO audit this ala man page
            // expand_parameter, commands, and arithmetic
            if (x && !(ss = sss = expand_one_arg(ss,
              NO_PATH|NO_SPLIT|NO_BRACE|NO_TILDE|NO_QUOTE, 0)))
            {
              s = 0;
              break;
            }

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
    } else if (*ss == '&' || ss[1] == '&') {

      // is there an explicit fd?
      for (ss = sss; isdigit(*ss); ss++);
      if (ss-sss>5 || (*ss && (*ss != '-' || ss[1]))) {
        if (*ss=='&') ss++;
        saveclose = 4;
        goto notfd;
      }

      from = (ss==sss) ? to : atoi(sss);
      saveclose = 2-(*ss == '-');
    } else {
notfd:
      // Permissions to open external file with: < > >> <& >& <> >| &>> &>
      if (!strcmp(ss, "<>")) from = O_CREAT|O_RDWR;
      else if (strstr(ss, ">>")) from = O_CREAT|O_APPEND|O_WRONLY;
      else {
        from = (*ss == '<') ? O_RDONLY : O_CREAT|O_WRONLY|O_TRUNC;
        if (!strcmp(ss, ">") && (TT.options&OPT_NOCLOBBER)) {
          struct stat st;

          // Not _just_ O_EXCL: > /dev/null allowed
          if (stat(sss, &st) || !S_ISREG(st.st_mode)) from |= O_EXCL;
        }
      }

      // we expect /dev/fd/# and /dev/{stdin,stdout,stderr} to be in /dev

// TODO: /dev/{tcp,udp}/host/port

      // Open the file
      if (-1 == (from = xcreate_stdio(sss, from|WARN_ONLY, 0666))) {
        s = 0;

        break;
      }
    }

    // perform redirect, saving displaced "to".
    if (save_redirect(&pp->urd, from, to)) bad++;
    // Do we save displaced "to" in env variable instead of undo list?
    if (cv) {
      --*pp->urd;
      if (!setvar(cv)) bad++;
      cv = 0;
    }
    if ((saveclose&1) && save_redirect(&pp->urd, -1, from)) bad++;
    if ((saveclose&4) && save_redirect(&pp->urd, from, 2)) bad++;
    if (!(saveclose&2)) close(from);
    if (bad) break;
  }

  // didn't parse everything?
  if (j != arg->c) {
    if (s) syntax_err(s);
    if (!pp->exit) pp->exit = 1;
    free(cv);
  }

  return pp;
}

static void shexec(char *cmd, char **argv)
{
  xsetenv(xmprintf("_=%s", cmd), 0);
  execve(cmd, argv, environ);
  if (errno == ENOEXEC) run_subshell("source \"$_\"", 11);
}

// Call binary, or run via child shell
static void sh_exec(char **argv)
{
  char *pp = getvar("PATH" ? : _PATH_DEFPATH), *cc = TT.isexec ? : *argv;
  struct string_list *sl;

  if (getpid() != TT.pid) signal(SIGINT, SIG_DFL);
  if (strchr(cc, '/')) shexec(cc, argv);
  else for (sl = find_in_path(pp, cc); sl; free(llist_pop(&sl)))
    shexec(sl->str, argv);

  perror_msg("%s", *argv);
  if (!TT.isexec) _exit(127);
}

// Execute a single command
static struct sh_process *run_command(struct sh_arg *arg)
{
  char *s, *ss = 0, *sss, **old = environ;
  struct sh_arg env;
  int envlen, jj, ll;
  struct sh_process *pp;
  struct toy_list *tl;

if (BUGBUG) dprintf(255, "run_command %s\n", arg->v[0]);

  // Grab leading variable assignments
  for (envlen = 0; envlen<arg->c; envlen++) {
    s = varend(arg->v[envlen]);
    if (s == arg->v[envlen] || *s != '=') break;
  }

  // expand arguments and perform redirects
  pp = expand_redir(arg, envlen, 0);

if (BUGBUG) { int i; dprintf(255, "envlen=%d arg->c=%d run=", envlen, arg->c); for (i=0; i<pp->arg.c; i++) dprintf(255, "'%s' ", pp->arg.v[i]); dprintf(255, "\n"); }
  // perform assignments locally if there's no command
  if (envlen == arg->c) {
    for (jj = 0; jj<envlen; jj++) {
      if (!(s = expand_one_arg(arg->v[jj], NO_PATH|NO_SPLIT, 0))) break;
      if (s == arg->v[jj]) s = xstrdup(s);
      setvar(s);
    }
    goto out;
  }

  // assign leading environment variables (if any) in temp environ copy
  jj = 0;
  env.v = 0;
  if (envlen) {
    for (env.c = 0; environ[env.c]; env.c++);
    memcpy(env.v = xmalloc(sizeof(char *)*(env.c+33)), environ,
      sizeof(char *)*(env.c+1));
    for (; jj<envlen; jj++) {
      if (!(sss = expand_one_arg(arg->v[jj], NO_PATH|NO_SPLIT, &pp->delete)))
        break;
      for (ll = 0; ll<env.c; ll++) {
        for (s = sss, ss = env.v[ll]; *s == *ss && *s != '='; s++, ss++);
        if (*s != '=') continue;
        env.v[ll] = sss;
        break;
      }
      if (ll == env.c) arg_add(&env, sss);
    }
    environ = env.v;
  }

  // Do nothing if nothing to do
  if (jj != envlen || pp->exit || !pp->arg.v);
//  else if (!strcmp(*pp->arg.v, "(("))
// TODO: handle ((math)) currently totally broken
// TODO: call functions()
  // Is this command a builtin that should run in this process?
  else if ((tl = toy_find(*pp->arg.v))
    && (tl->flags & (TOYFLAG_NOFORK|TOYFLAG_MAYFORK)))
  {
    sigjmp_buf rebound;
    char temp[jj = offsetof(struct toy_context, rebound)];

    // This fakes lots of what toybox_main() does.
    memcpy(&temp, &toys, jj);
    memset(&toys, 0, jj);

    // If we give the union in TT a name, the compiler complains
    // "declaration does not declare anything", but if we DON'T give it a name
    // it accepts it. So we can't use the union's type name here, and have
    // to offsetof() the first thing _after_ the union to get the size.
    memset(&TT, 0, offsetof(struct sh_data, lineno));

    TT.pp = pp;
    if (!sigsetjmp(rebound, 1)) {
      toys.rebound = &rebound;
      toy_singleinit(tl, pp->arg.v);  // arg.v must be null terminated
      tl->toy_main();
      xflush(0);
    }
    TT.pp = 0;
    toys.rebound = 0;
    pp->exit = toys.exitval;
    if (toys.optargs != toys.argv+1) free(toys.optargs);
    if (toys.old_umask) umask(toys.old_umask);
    memcpy(&toys, &temp, jj);
  } else if (-1==(pp->pid = xpopen_setup(pp->arg.v+envlen, 0, sh_exec)))
    perror_msg("%s: vfork", *pp->arg.v);

  // Restore environment variables
  environ = old;
  free(env.v);

out:
  setvarval("_", (envlen == arg->c) ? "" : s);
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

// if then fi for while until select done done case esac break continue return

// Free one pipeline segment.
static void free_pipeline(void *pipeline)
{
  struct sh_pipeline *pl = pipeline;
  int i, j;

  // free arguments and HERE doc contents
  if (pl) for (j=0; j<=pl->count; j++) {
    if (!pl->arg[j].v) continue;
    for (i = 0; i<=pl->arg[j].c; i++) free(pl->arg[j].v[i]);
    free(pl->arg[j].v);
  }
  free(pl);
}

static void free_function(struct sh_function *sp)
{
  llist_traverse(sp->pipeline, free_pipeline);
  llist_traverse(sp->expect, free);
  memset(sp, 0, sizeof(struct sh_function));
}

// TODO this has to add to a namespace context. Functions within functions...
static struct sh_pipeline *add_function(char *name, struct sh_pipeline *pl)
{
dprintf(2, "stub add_function");

  return pl->end;
}

// Add a line of shell script to a shell function. Returns 0 if finished,
// 1 to request another line of input (> prompt), -1 for syntax err
static int parse_line(char *line, struct sh_function *sp)
{
  char *start = line, *delete = 0, *end, *last = 0, *s, *ex, done = 0,
    *tails[] = {"fi", "done", "esac", "}", "]]", ")", 0};
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
      for (s = line, end = arg->v[arg->c]; *s && *end; s++) {
        s += strspn(s, "\\\"'");
        if (*s != *end) break;
      }
      // Add this line, else EOF hit so end HERE document
      if (!*s && !*end) {
        end = arg->v[arg->c];
        arg_add(arg, xstrdup(line));
        arg->v[arg->c] = end;
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

if (BUGBUG>1) dprintf(255, "{%d:%s}\n", pl->type, ex ? ex : (sp->expect ? "*" : ""));

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
    if ((end = parse_word(start, 0)) == (void *)1) goto flush;

if (BUGBUG>1) dprintf(255, "[%.*s:%s] ", end ? (int)(end-start) : 0, start, ex ? : "");
    // Is this a new pipeline segment?
    if (!pl) {
      pl = xzalloc(sizeof(struct sh_pipeline));
      pl->end = pl;
      arg = pl->arg;
      dlist_add_nomalloc((void *)&sp->pipeline, (void *)pl);
    }

    // Do we need to request another line to finish word (find ending quote)?
    if (!end) {
      // Save unparsed bit of this line, we'll need to re-parse it.
      arg_add(arg, xstrndup(start, strlen(start)));
      arg->c = -arg->c;
      free(delete);

      return 1;
    }

    // Ok, we have a word. What does it _mean_?

    // Did we hit end of line or ) outside a function declaration?
    // ) is only saved at start of a statement, ends current statement
    if (end == start || (arg->c && *start == ')' && pl->type!='f')) {
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
    arg_add(arg, s = xstrndup(start, end-start));
    start = end;

    if (strchr(";|&", *s) && strncmp(s, "&>", 2)) {
      arg->c--;

      // treat ; as newline so we don't have to check both elsewhere.
      if (!strcmp(s, ";")) {
        arg->v[arg->c] = 0;
        free(s);
        s = 0;
// TODO enforce only one ; allowed between "for i" and in or do.
        if (!arg->c && ex && !memcmp(ex, "do\0C", 4)) continue;

      // ;; and friends only allowed in case statements
      } else if (*s == ';' && (!ex || strcmp(ex, "esac"))) goto flush;
      last = s;

      // flow control without a statement is an error
      if (!arg->c) goto flush;
      pl->count = -1;

      continue;
    }

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
          if (pl->prev->type == 's') goto flush;
          if (!strncmp(pl->prev->arg->v[1], "((", 2)) goto flush;
          else if (strcmp(s, "in")) goto flush;
          pl->type = 's';

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
    if (!ex && sp->expect) free(dlist_lpop(&sp->expect));

    // Did we start a new statement
    if (end) {
      pl->type = 1;

      // Only innermost statement needed in { { { echo ;} ;} ;} and such
      if (sp->expect && !sp->expect->prev->data) free(dlist_lpop(&sp->expect));

    // if can't end a statement here skip next few tests
    } else if (!ex);

    // If we got here we expect a specific word to end this block: is this it?
    else if (!strcmp(s, ex)) {
      // can't "if | then" or "while && do", only ; & or newline works
      if (last && strcmp(last, "&")) goto flush;

      // consume word, record block end location in earlier !0 type blocks
      free(dlist_lpop(&sp->expect));
      if (3 == (pl->type = anystr(s, tails) ? 3 : 2)) {
        struct sh_pipeline *pl2 = pl;

        i = 0;
        for (i = 0; (pl2 = pl2->prev);) {
          if (pl2->type == 3) i++;
          else if (pl2->type) {
            if (!i) pl2->end = pl;
            if ((pl2->type == 1 || pl2->type == 'f') && --i<0) break;
          }
        }
      }

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

    // Queue up the next thing to expect, all preceded by a statement
    if (end) {
      if (!pl->type) pl->type = 2;

      dlist_add(&sp->expect, end);
      if (!anystr(end, tails)) dlist_add(&sp->expect, 0);
      pl->count = -1;
    }

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
  if (pl->arg->v[pl->arg->c] && strcmp(pl->arg->v[pl->arg->c], "&")) return 1;

  // Don't need more input, can start executing.

  dlist_terminate(sp->pipeline);
  return 0;

flush:
  if (s) syntax_err(s);
  free_function(sp);

  return 0-!!s;
}

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
    rc = pp->not ? !pp->exit : pp->exit;
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
    if (save_redirect(urd, *pipes, 0)) return 1;
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
    if (save_redirect(urd, pipes[1], 1)) {
      close(pipes[0]);
      close(pipes[1]);

      return 1;
    }
    if (pipes[1] != 1) close(pipes[1]);
    fcntl(*pipes, F_SETFD, FD_CLOEXEC);
    if (ctl[1] == '&') save_redirect(urd, 1, 2);
  }

  return 0;
}

struct blockstack {
  struct blockstack *next;
  struct sh_pipeline *start, *middle;
  struct sh_process *pp;       // list of processes piping in to us
  int run, loop, *urd, pout;
  struct sh_arg farg;          // for/select arg stack
  struct arg_list *fdelete;    // farg's cleanup list
  char *fvar;                  // for/select's iteration variable name
};

// when ending a block, free, cleanup redirects and pop stack.
static struct sh_pipeline *pop_block(struct blockstack **blist, int *pout)
{
  struct blockstack *blk = *blist;
  struct sh_pipeline *pl = blk->start->end;

  // when ending a block, free, cleanup redirects and pop stack.
  if (*pout != -1) close(*pout);
  *pout = blk->pout;
  unredirect(blk->urd);
  llist_traverse(blk->fdelete, free);
  free(llist_pop(blist));

  return pl;
}

// run a parsed shell function. Handle flow control blocks and characters,
// setup pipes and block redirection, break/continue, call builtins,
// vfork/exec external commands.
static void run_function(struct sh_pipeline *pl)
{
  struct blockstack *blk = 0, *new;
  struct sh_process *pplist = 0; // processes piping into current level
  int *urd = 0, pipes[2] = {-1, -1};
  long i;

// TODO: "echo | read i" is backgroundable with ctrl-Z despite read = builtin.
//       probably have to inline run_command here to do that? Implicit ()
//       also "X=42 | true; echo $X" doesn't get X.
//       I.E. run_subshell() here sometimes? (But when?)
// TODO: bash supports "break &" and "break > file". No idea why.
// TODO If we just started a new pipeline, implicit parentheses (subshell)
// TODO can't free sh_process delete until ready to dispose else no debug output
/*
TODO: a | b | c needs subshell for builtins?
        - anything that can produce output
        - echo declare dirs
      (a; b; c) like { } but subshell
      when to auto-exec? ps vs sh -c 'ps' vs sh -c '(ps)'
*/


  TT.hfd = 10;

  // iterate through pipeline segments
  while (pl) {
    char *ctl = pl->end->arg->v[pl->end->arg->c],
      *s = *pl->arg->v, *ss = pl->arg->v[1];

    // Skip disabled blocks, handle pipes
    if (pl->type<2) {
      if (blk && !blk->run) {
        pl = pl->end->next;
        continue;
      }
      if (pipe_segments(ctl, pipes, &urd)) break;
    }

if (BUGBUG) dprintf(255, "%d runtype=%d %s %s\n", getpid(), pl->type, s, ctl);
    // Is this an executable segment?
    if (!pl->type) {

      // Is it a flow control jump? These aren't handled as normal builtins
      // because they move *pl to other pipeline segments which is local here.
      if (!strcmp(s, "break") || !strcmp(s, "continue")) {

        // How many layers to peel off?
        i = ss ? atol(ss) : 0;
        if (i<1) i = 1;
        if (!blk || pl->arg->c>2 || ss[strspn(ss, "0123456789")]) {
          syntax_err(s);
          break;
        }

        while (i && blk)
          if (!--i && *s == 'c') pl = blk->start;
          else pl = pop_block(&blk, pipes);
        if (i) {
          syntax_err("break");
          break;
        }
      } else {
        // Parse and run next command, saving resulting process
        dlist_add_nomalloc((void *)&pplist, (void *)run_command(pl->arg));

        // Three cases: backgrounded&, pipelined|, last process in pipeline;
        if (ctl && !strcmp(ctl, "&")) {
          pplist->job = ++TT.jobcnt;
          arg_add(&TT.jobs, (void *)pplist);
          pplist = 0;
        }
      }

    // Start of flow control block?
    } else if (pl->type == 1) {
      struct sh_process *pp = 0;
      int rc;

      // Save new block and add it to the stack.
      new = xzalloc(sizeof(*blk));
      new->next = blk;
      blk = new;
      blk->start = pl;
      blk->run = 1;

      // push pipe and redirect context into block
      blk->pout = *pipes;
      *pipes = -1;
      pp = expand_redir(pl->end->arg, 1, blk->urd = urd);
      urd = 0;
      rc = pp->exit;
      if (pp->arg.c) {
        syntax_err(*pp->arg.v);
        rc = 1;
      }

      // Cleanup if we're not doing a subshell
      if (rc || strcmp(s, "(")) {
        llist_traverse(pp->delete, free);
        free(pp);
        if (rc) {
          toys.exitval = rc;
          break;
        }
      } else {
        // Create new process
        if (!CFG_TOYBOX_FORK) {
          ss = pl2str(pl->next);
          pp->pid = run_subshell(ss, strlen(ss));
          free(ss);
        } else if (!(pp->pid = fork())) {
          run_function(pl->next);
          _exit(toys.exitval);
        }

        // add process to current pipeline same as type 0
        dlist_add_nomalloc((void *)&pplist, (void *)pp);
        pl = pl->end;
        continue;
      }
pp = 0;

      // What flow control statement is this?

      // {/} if/then/elif/else/fi, while until/do/done - no special handling

      // for select/do/done: populate blk->farg with expanded arguments (if any)
      if (!strcmp(s, "for") || !strcmp(s, "select")) {
        if (blk->loop); // TODO: still needed?
        else if (!strncmp(blk->fvar = ss, "((", 2)) {
          blk->loop = 1;
dprintf(2, "TODO skipped init for((;;)), need math parser\n");

        // in LIST
        } else if (pl->next->type == 's') {
          for (i = 1; i<pl->next->arg->c; i++)
            if (expand_arg(&blk->farg, pl->next->arg->v[i], 0, &blk->fdelete))
              break;
          if (i != pl->next->arg->c) pl = pop_block(&blk, pipes);
        // in without LIST. (This expansion can't fail.)
        } else expand_arg(&blk->farg, "\"$@\"", 0, &blk->fdelete);

// TODO case/esac [[/]] ((/)) function/}

      }

    // gearshift from block start to block body (end of flow control test)
    } else if (pl->type == 2) {

      blk->middle = pl;

      // Handle if/else/elif statement
      if (!strcmp(s, "then")) blk->run = blk->run && !toys.exitval;
      else if (!strcmp(s, "else") || !strcmp(s, "elif")) blk->run = !blk->run;

      // Loop
      else if (!strcmp(s, "do")) {
        ss = *blk->start->arg->v;
        if (!strcmp(ss, "while")) blk->run = blk->run && !toys.exitval;
        else if (!strcmp(ss, "until")) blk->run = blk->run && toys.exitval;
        else if (blk->loop >= blk->farg.c) pl = pop_block(&blk, pipes);
        else if (!strncmp(blk->fvar, "((", 2)) {
dprintf(2, "TODO skipped running for((;;)), need math parser\n");
        } else setvarval(blk->fvar, blk->farg.v[blk->loop++]);
      }

    // end of block, may have trailing redirections and/or pipe
    } else if (pl->type == 3) {

      // if we end a block we're not in, we started in a block (subshell)
      if (!blk) break;

      // repeating block?
      if (blk->run && !strcmp(s, "done")) {
        pl = blk->middle;
        continue;
      }

      pop_block(&blk, pipes);
    } else if (pl->type == 'f') pl = add_function(s, pl);

    // If we ran a process and didn't pipe output or background, wait for exit
    if (pplist && *pipes == -1) {
      toys.exitval = wait_pipeline(pplist);
      llist_traverse(pplist, free_process);
      pplist = 0;
    }

    // for && and || skip pipeline segment(s) based on return code
    if (pl->type == 1 || pl->type == 3)
      while (ctl && !strcmp(ctl, toys.exitval ? "&&" : "||"))
        ctl = (pl = pl->type ? pl->end : pl->next)?pl->arg->v[pl->arg->c]:0;

    pl = pl->next;
  }

  // did we exit with unfinished stuff?
  // TODO: current context isn't packaged into a block, so can't just pop it
  if (*pipes != -1) close(*pipes);
  if (pplist) {
    toys.exitval = wait_pipeline(pplist);
    llist_traverse(pplist, free_process);
  }
  unredirect(urd);

  // Cleanup from syntax_err();
  while (blk) pop_block(&blk, pipes);
}

// Parse and run a self-contained command line with no prompt/continuation
static int sh_run(char *new)
{
  struct sh_function scratch;

// TODO switch the fmemopen for -c to use this? Error checking? $(blah)

  memset(&scratch, 0, sizeof(struct sh_function));
  if (!parse_line(new, &scratch)) run_function(scratch.pipeline);
  free_function(&scratch);

  return toys.exitval;
}

// Print prompt to stderr, parsing escapes
// Truncated to 4k at the moment, waiting for somebody to complain.
static void do_prompt(char *prompt)
{
  char *s, *ss, c, cc, *pp = toybuf;
  int len, ll;

  if (!prompt) return;
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

// only set local variable when global not present, does not extend array
static struct sh_vars *initlocal(char *name, char *val)
{
  return addvar(xmprintf("%s=%s", name, val ? val : ""));
}

static struct sh_vars *initlocaldef(char *name, char *val, char *def)
{
  return initlocal(name, (!val || !*val) ? def : val);
}

// export existing "name" or assign/export name=value string (making new copy)
static void export(char *str)
{
  struct sh_vars *shv = 0;
  char *s;

  // Make sure variable exists and is updated
  if (strchr(str, '=')) shv = setvar(xstrdup(str));
  else if (!(shv = findvar(str))) shv = addvar(str = xmprintf("%s=", str));
  if (!shv || (shv->flags&VAR_GLOBAL)) return;

  // Resolve local magic for export
  if (shv->flags&VAR_MAGIC) {
    s = shv->str;
    shv->str = xmprintf("%.*s=%s", (int)(varend(str)-str), str, getvar(str));
    free(s);
  }

  xsetenv(shv->str, 0);
  shv->flags |= VAR_GLOBAL;
}

static void unexport(char *str)
{
  struct sh_vars *shv = findvar(str);

  if (shv) {
    if (shv->flags&VAR_GLOBAL) shv->str = xpop_env(str);
    shv->flags &=~VAR_GLOBAL;
  }
  if (strchr(str, '=')) setvar(str);
}

// init locals, sanitize environment, handle nommu subshell handoff
static void subshell_setup(void)
{
  int ii, to, from, pid, ppid, zpid, myppid = getppid(), len, uid = getuid();
  struct passwd *pw = getpwuid(uid);
  char *s, *ss, *magic[] = {"SECONDS","RANDOM","LINENO","GROUPS"},
    *readonly[] = {xmprintf("EUID=%d", geteuid()), xmprintf("UID=%d", uid),
                   xmprintf("PPID=%d", myppid)};
  struct stat st;
  struct utsname uu;
  FILE *fp;

  // Initialize magic and read only local variables
  srandom(TT.SECONDS = millitime());
  for (ii = 0; ii<ARRAY_LEN(magic); ii++)
    initlocal(magic[ii], "")->flags = VAR_MAGIC|(VAR_INT*('G'!=*magic[ii]));
  for (ii = 0; ii<ARRAY_LEN(readonly); ii++)
    addvar(readonly[ii])->flags = VAR_READONLY|VAR_INT;

  // Add local variables that can be overwritten
  initlocal("PATH", _PATH_DEFPATH);
  if (!pw) pw = (void *)toybuf; // first use, so still zeroed
  sprintf(toybuf+1024, "%u", uid);
  initlocaldef("HOME", pw->pw_dir, "/");
  initlocaldef("SHELL", pw->pw_shell, "/bin/sh");
  initlocaldef("USER", pw->pw_name, toybuf+1024);
  initlocaldef("LOGNAME", pw->pw_name, toybuf+1024);
  gethostname(toybuf, sizeof(toybuf)-1);
  initlocal("HOSTNAME", toybuf);
  uname(&uu);
  initlocal("HOSTTYPE", uu.machine);
  sprintf(toybuf, "%s-unknown-linux", uu.machine);
  initlocal("MACHTYPE", toybuf);
  initlocal("OSTYPE", uu.sysname);
  // sprintf(toybuf, "%s-toybox", TOYBOX_VERSION);
  // initlocal("BASH_VERSION", toybuf);
  initlocal("OPTERR", "1"); // TODO: test if already exported?
  if (readlink0("/proc/self/exe", s = toybuf, sizeof(toybuf))||(s=getenv("_")))
    initlocal("BASH", s);
  initlocal("PS2", "> ");

  // Ensure environ copied and toys.envc set, and clean out illegal entries
  TT.ifs = " \t\n";
  xsetenv("", 0);
  for (to = from = pid = ppid = zpid = 0; (s = environ[from]); from++) {

    // If nommu subshell gets handoff
    if (!CFG_TOYBOX_FORK && !toys.stacktop) {
      len = 0;
      sscanf(s, "@%d,%d%n", &pid, &ppid, &len);
      if (s[len]) pid = ppid = 0;
      if (*s == '$' && s[1] == '=') zpid = atoi(s+2);
// TODO marshall $- to subshell like $$
    }

    // Filter out non-shell variable names from inherited environ.
    // (haven't xsetenv() yet so no need to free() or adjust toys.envc)
    ss = varend(s);
    if (*ss == '=') {
      struct sh_vars *shv = findvar(s);

      if (!shv) addvar(environ[from])->flags = VAR_GLOBAL;
      else if (shv->flags&VAR_READONLY) continue;
      else {
        shv->flags |= VAR_GLOBAL;
        free(shv->str);
        shv->str = s;
      }
      environ[to++] = s;
    }
    if (!memcmp(s, "IFS=", 4)) TT.ifs = s+4;
  }
  environ[toys.optc = to] = 0;

  // set/update PWD
  sh_run("cd .");

  // set _ to path to this shell
  s = toys.argv[0];
  ss = 0;
  if (!strchr(s, '/')) {
    if ((ss = getcwd(0, 0))) {
      s = xmprintf("%s/%s", ss, s);
      free(ss);
      ss = s;
    } else if (*toybuf) s = toybuf;
  }
  s = xsetenv("_", s);
  if (!findvar(s)) addvar(s)->flags = VAR_GLOBAL;
  free(ss);
  if (!(ss = getvar("SHLVL"))) export("SHLVL=1");
  else {
    char buf[16];

    sprintf(buf, "%u", atoi(ss+6)+1);
    xsetenv("SHLVL", buf);
    export("SHLVL");
  }

//TODO indexed array,associative array,integer,local,nameref,readonly,uppercase
//          if (s+1<ss && strchr("aAilnru", *s)) {

  // sanity check: magic env variable, pipe status
  if (CFG_TOYBOX_FORK || toys.stacktop || pid!=getpid() || ppid!=myppid) return;
  if (fstat(254, &st) || !S_ISFIFO(st.st_mode)) error_exit(0);
  TT.pid = zpid;
  fcntl(254, F_SETFD, FD_CLOEXEC);
  fp = fdopen(254, "r");

  // This is not efficient, could array_add the local vars.
// TODO implicit exec when possible
  while ((s = xgetline(fp, 0))) toys.exitval = sh_run(s);
  fclose(fp);

  xexit();
}

void sh_main(void)
{
  char *new, *cc = TT.sh.c;
  struct sh_function scratch;
  int prompt = 0;
  struct string_list *sl = 0;
  struct sh_arg arg;
  FILE *f;

  signal(SIGPIPE, SIG_IGN);
  TT.options = OPT_BRACE;

  TT.pid = getpid();
  TT.SECONDS = time(0);
  TT.arg = &arg;
  if (!(arg.c = toys.optc)) {
    arg.v = xmalloc(2*sizeof(char *));
    arg.v[arg.c++] = *toys.argv;
    arg.v[arg.c] = 0;
  } else memcpy(arg.v = xmalloc((arg.c+1)*sizeof(char *)), toys.optargs,
      (arg.c+1)*sizeof(char *));

  // TODO euid stuff?
  // TODO login shell?
  // TODO read profile, read rc

  // if (!FLAG(noprofile)) { }

if (BUGBUG) { int fd = open("/dev/tty", O_RDWR); if (fd == -1) fd = open("/dev/console", O_RDWR); if (fd == -1) dup2(2, 255); else dup2(fd, 255); close(fd); }

  // Is this an interactive shell?
  if (FLAG(s) || (!FLAG(c) && !toys.optc)) TT.options |= OPT_S;
  if (FLAG(i) || (!FLAG(c) && (TT.options&OPT_S) && isatty(0)))
    TT.options |= OPT_I;
  if (FLAG(c)) TT.options |= OPT_C;

  // Read environment for exports from parent shell. Note, calls run_sh()
  // which blanks argument sections of TT and this, so parse everything
  // we need from shell command line before that.
  subshell_setup();
  if (TT.options&OPT_I) {
    if (!getvar("PS1")) setvarval("PS1", getpid() ? "\\$ " : "# ");
    // TODO Set up signal handlers and grab control of this tty.
    // ^C SIGINT ^\ SIGQUIT ^Z SIGTSTP SIGTTIN SIGTTOU SIGCHLD
    // setsid(), setpgid(), tcsetpgrp()...
    xsignal(SIGINT, SIG_IGN);
  }

  memset(&scratch, 0, sizeof(scratch));

// TODO unify fmemopen() here with sh_run
  if (cc) f = fmemopen(cc, strlen(cc), "r");
  else if (TT.options&OPT_S) f = stdin;
// TODO: syntax_err should exit from shell scripts
  else if (!(f = fopen(*toys.optargs, "r"))) {
    char *pp = getvar("PATH") ? : _PATH_DEFPATH;

    for (sl = find_in_path(pp, *toys.optargs); sl; free(llist_pop(&sl)))
      if ((f = fopen(sl->str, "r"))) break;
    if (sl) llist_traverse(sl->next, free);
    else perror_exit_raw(*toys.optargs);
  }

  // Loop prompting and reading lines
  for (;;) {
    TT.lineno++;
    if ((TT.options&(OPT_I|OPT_S|OPT_C)) == (OPT_I|OPT_S))
      do_prompt(getvar(prompt ? "PS2" : "PS1"));

// TODO line editing/history, should set $COLUMNS $LINES and sigwinch update
    if (!(new = xgetline(f, 0))) {
// TODO: after first EINTR getline returns always closed?
      if (errno != EINTR) break;
      free_function(&scratch);
      prompt = 0;
      if (f != stdin) break;
      continue;
// TODO: ctrl-z during script read having already read partial line,
// SIGSTOP and SIGTSTP need need SA_RESTART, but child proc should stop
    }

if (BUGBUG) dprintf(255, "line=%s\n", new);
    if (sl) {
      if (*new == 0x7f) error_exit("'%s' is ELF", sl->str);
      free(sl);
      sl = 0;
    }
// TODO if (!isspace(*new)) add_to_history(line);

    // returns 0 if line consumed, command if it needs more data
    prompt = parse_line(new, &scratch);
if (BUGBUG) dprintf(255, "prompt=%d\n", prompt), dump_state(&scratch);
    if (prompt != 1) {
// TODO: ./blah.sh one two three: put one two three in scratch.arg
      if (!prompt) run_function(scratch.pipeline);
      free_function(&scratch);
      prompt = 0;
    }
    free(new);
  }

  if (prompt) error_exit("%ld:unfinished line"+4*!TT.lineno, TT.lineno);
}

/********************* shell builtin functions *************************/

#define CLEANUP_sh
#define FOR_cd
#include "generated/flags.h"
void cd_main(void)
{
  char *home = getvar("HOME") ? : "/", *pwd = getvar("PWD"), *from, *to = 0,
    *dd = xstrdup(*toys.optargs ? *toys.optargs : home);
  int bad = 0;

  // TODO: CDPATH? Really?

  // prepend cwd or $PWD to relative path
  if (*dd != '/') {
    from = pwd ? : (to = getcwd(0, 0));
    if (!from) setvarval("PWD", "(nowhere)");
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
    if (pwd) {
      setvarval("OLDPWD", pwd);
      if (TT.cdcount == 1) {
        export("OLDPWD");
        TT.cdcount++;
      }
    }
    setvarval("PWD", dd);
    if (!TT.cdcount) {
      export("PWD");
      TT.cdcount++;
    }
  }
  free(dd);
}

void exit_main(void)
{
  exit(*toys.optargs ? atoi(*toys.optargs) : 0);
}

void unset_main(void)
{
  char **arg, *s;

  for (arg = toys.optargs; *arg; arg++) {
    s = varend(*arg);
    if (s == *arg || *s) {
      error_msg("bad '%s'", *arg);
      continue;
    }

    // unset magic variable?
    if (!strcmp(*arg, "IFS")) TT.ifs = " \t\n";
    unsetvar(*arg);
  }
}

#define CLEANUP_cd
#define FOR_export
#include "generated/flags.h"

void export_main(void)
{
  char **arg, *eq;

  // list existing variables?
  if (!toys.optc) {
    for (arg = environ; *arg; arg++) xprintf("declare -x %s\n", *arg);
    return;
  }

  // set/move variables
  for (arg = toys.optargs; *arg; arg++) {
    eq = varend(*arg);
    if (eq == *arg || (*eq && *eq != '=')) {
      error_msg("bad %s", *arg);
      continue;
    }

    if (FLAG(n)) unexport(*arg);
    else export(*arg);
  }
}

void eval_main(void)
{
  struct sh_arg *old = TT.arg, new = {toys.argv, toys.optc+1};
  char *s;

  TT.arg = &new;
  s = expand_one_arg("\"$*\"", SEMI_IFS, 0);
  TT.arg = old;
  sh_run(s);
  free(s);
}

#define CLEANUP_export
#define FOR_exec
#include "generated/flags.h"

void exec_main(void)
{
  char *ee[1] = {0}, **old = environ;

  // discard redirects and return if nothing to exec
  free(TT.pp->urd);
  TT.pp->urd = 0;
  if (!toys.optc) return;

  // exec, handling -acl
  TT.isexec = *toys.optargs;
  if (FLAG(c)) environ = ee;
  if (TT.exec.a || FLAG(l))
    *toys.optargs = xmprintf("%s%s", FLAG(l) ? "-" : "", TT.exec.a?:TT.isexec);
  sh_exec(toys.optargs);

  // report error (usually ENOENT) and return
  perror_msg("%s", TT.isexec);
  TT.isexec = 0;
  toys.exitval = 127;
  environ = old;
}

void shift_main(void)
{
  long long by = 1;

  if (toys.optc) by = atolx(*toys.optargs);
  by += TT.shift;
  if (by<0 || by>= TT.arg->c) toys.exitval++;
  else TT.shift = by;
}
