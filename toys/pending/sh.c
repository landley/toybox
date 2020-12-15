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
 * deviations from posix: don't care about $LANG or $LC_ALL

 * builtins: alias bg command fc fg getopts jobs newgrp read umask unalias wait
 *          disown suspend source pushd popd dirs logout times trap cd hash exit
 *           unset local export readonly set : . let history declare ulimit type
 * "special" builtins: break continue eval exec return shift
 * external with extra shell behavior: kill pwd time test

 * * ? [ # ~ = % [[ ]] function select exit label:

 * TODO: case, wildcard +(*|?), job control (find_plus_minus), ${x//}, $(())

 * TODO: support case in $() because $(case a in a) ;; ; esac) stops at first )
 * TODO: test exit from "trap EXIT" doesn't recurse
 * TODO: ! history expansion
 * TODO: getuid() vs geteuid()
 * TODO: test that $PS1 color changes work without stupid \[ \] hack
 * TODO: Handle embedded NUL bytes in the command line? (When/how?)
 * TODO: set -e -u -o pipefail, shopt -s nullglob
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
USE_SH(NEWTOY(set, 0, TOYFLAG_NOFORK))
USE_SH(NEWTOY(shift, ">1", TOYFLAG_NOFORK))
USE_SH(NEWTOY(source, "0<1", TOYFLAG_NOFORK))
USE_SH(OLDTOY(., source, TOYFLAG_NOFORK))
USE_SH(NEWTOY(unset, "fvn", TOYFLAG_NOFORK))

USE_SH(NEWTOY(sh, "0(noediting)(noprofile)(norc)sc:i", TOYFLAG_BIN))
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

config SET
  bool
  default n
  depends on SH
  help
    usage: set [+a] [+o OPTION] [VAR...]

    Set variables and shell attributes. Use + to disable and - to enable.
    NAME=VALUE arguments assign to the variable, any leftovers set $1, $2...
    With no arguments, prints current variables.

    -f	NAME is a function
    -v	NAME is a variable
    -n	dereference NAME and unset that

    OPTIONs:
      history - enable command history

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

config SOURCE
  bool
  default n
  depends on SH
  help
    usage: source FILE [ARGS...]

    Read FILE and execute commands. Any ARGS become positional parameters.
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

  // keep ifs here: used to work around compiler limitation in run_command()
  char *ifs, *isexec, *wcpat;
  unsigned options, jobcnt;
  int hfd, pid, bangpid, varslen, cdcount;
  long long SECONDS;

  // global and local variables
  struct sh_vars {
    long flags;
    char *str;
  } *vars;

  // Parsed functions
  struct sh_function {
    char *name;
    struct sh_pipeline {  // pipeline segments: linked list of arg w/metadata
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
    struct sh_arg *raw, arg;
  } *pp; // currently running process

  struct sh_callstack {
    struct sh_callstack *next;
    struct sh_function scratch;
    struct sh_arg arg;
    struct arg_list *delete;
    long lineno, shift;
  } *cc;

  // job list, command line for $*, scratch space for do_wildcard_files()
  struct sh_arg jobs, *wcdeck;
)

// Can't yet avoid this prototype. Fundamental problem is $($($(blah))) nests,
// leading to function loop with run->parse->run
static int sh_run(char *new);

// ordered for greedy matching, so >&; becomes >& ; not > &;
// making these const means I need to typecast the const away later to
// avoid endless warnings.
static const char *redirectors[] = {"<<<", "<<-", "<<", "<&", "<>", "<", ">>",
  ">&", ">|", ">", "&>>", "&>", 0};

// The order of these has to match the string in set_main()
#define OPT_B	0x100
#define OPT_C	0x200
#define OPT_x	0x400

static void syntax_err(char *s)
{
  error_msg("syntax error: %s", s);
  toys.exitval = 2;
  if (!(TT.options&FLAG_i)) xexit();
}

// append to array with null terminator and realloc as necessary
static void arg_add(struct sh_arg *arg, char *data)
{
  // expand with stride 32. Micro-optimization: don't realloc empty stack
  if (!(arg->c&31) && (arg->c || !arg->v))
    arg->v = xrealloc(arg->v, sizeof(char *)*(arg->c+33));
  arg->v[arg->c++] = data;
  arg->v[arg->c] = 0;
}

// add argument to an arg_list
static void *push_arg(struct arg_list **list, void *arg)
{
  struct arg_list *al;

  if (list) {
    al = xmalloc(sizeof(struct arg_list));
    al->next = *list;
    al->arg = arg;
    *list = al;
  }

  return arg;
}

static void arg_add_del(struct sh_arg *arg, char *data,struct arg_list **delete)
{
  arg_add(arg, push_arg(delete, data));
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
long long do_math(char **s)
{
  long long ll;

  while (isspace(**s)) ++*s;
  ll = strtoll(*s, s, 0);
  while (isspace(**s)) ++*s;

  return ll;
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
  if (!strncmp(s, "IFS=", 4)) TT.ifs = s+4;
  if (!(var = findvar(s))) return addvar(s);
  flags = var->flags;

  if (flags&VAR_READONLY) {
    error_msg("%.*s: read only", len, s);
    free(s);

    return 0;
  }

// TODO if (flags&(VAR_TOUPPER|VAR_TOLOWER)) 
// unicode _is stupid enough for upper/lower case to be different utf8 byte
// lengths. example: lowercase of U+0130 (C4 B0) is U+0069 (69)
// TODO VAR_INT
// TODO VAR_ARRAY VAR_DICT

  if (flags&VAR_MAGIC) {
    char *ss = s+len-1;

// TODO: trailing garbage after do_math()?
    if (*s == 'S') TT.SECONDS = millitime() - 1000*do_math(&ss);
    else if (*s == 'R') srandom(do_math(&ss));
  } else if (flags&VAR_GLOBAL) xsetenv(var->str = s, 0);
  else {
    free(var->str);
    var->str = s;
  }

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
    else if (c == 'L') sprintf(toybuf, "%ld", TT.cc->lineno);
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
  ss = xmalloc(len+15);

  out = ss + sprintf(ss, "declare -%s \"", out);
  while (*types) {
    if (strchr("$\"\\`", *types)) *out++ = '\\';
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
// quote is depth of existing quote stack in toybuf (usually 0)
static char *parse_word(char *start, int early, int quote)
{
  int i, q, qc = 0;
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
      if (!end[1] || (end[1]=='\n' && !end[2])) return early ? end+1 : 0;
      end += 2;
    } else if (*end == '$' && -1 != (i = stridx("({[", end[1]))) {
      end++;
      if (strstart(&end, "((")) toybuf[quote++] = 255;
      else {
        toybuf[quote++] = ")}]"[i];
        end++;
      }
    } else if (end[1]=='(' && strchr("?*+@!", *end)) {
      toybuf[quote++] = ')';
      end += 2;
    }

    if (early && !quote) return end;
    end++;
  }

  return (quote && !early) ? 0 : end;
}

// Return next available high (>=10) file descriptor
static int next_hfd()
{
  int hfd;

  for (; TT.hfd<=99999; TT.hfd++) if (-1 == fcntl(TT.hfd, F_GETFL)) break;
  hfd = TT.hfd;
  if (TT.hfd>99999) {
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

    // dup "to"
    if (from >= 0 && to != dup2(from, to)) {
      if (hfd >= 0) close(hfd);

      return 1;
    }
  } else {
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

  for (i = 0; i<*urd; i++, rr += 2) if (rr[0] != -1) {
    // No idea what to do about fd exhaustion here, so Steinbach's Guideline.
    dup2(rr[0], rr[1]);
    close(rr[0]);
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
// if len, save length of next wc (whether or not it's in list)
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

// grab variable or special param (ala $$) up to len bytes. Return value.
// set *used to length consumed. Does not handle $* and $@
char *getvar_special(char *str, int len, int *used, struct arg_list **delete)
{
  char *s = 0, *ss, cc = *str;
  unsigned uu;

  *used = 1;
  if (cc == '-') {
    s = ss = xmalloc(8);
    if (TT.options&FLAG_i) *ss++ = 'i';
    if (TT.options&OPT_B) *ss++ = 'B';
    if (TT.options&FLAG_s) *ss++ = 's';
    if (TT.options&FLAG_c) *ss++ = 'c';
    *ss = 0;
  } else if (cc == '?') s = xmprintf("%d", toys.exitval);
  else if (cc == '$') s = xmprintf("%d", TT.pid);
  else if (cc == '#') s = xmprintf("%d", TT.cc->arg.c?TT.cc->arg.c-1:0);
  else if (cc == '!') s = xmprintf("%d"+2*!TT.bangpid, TT.bangpid);
  else {
    delete = 0;
    for (*used = uu = 0; *used<len && isdigit(str[*used]); ++*used)
      uu = (10*uu)+str[*used]-'0';
    if (*used) {
      if (uu) uu += TT.cc->shift;
      if (uu<TT.cc->arg.c) s = TT.cc->arg.v[uu];
    } else if ((*used = varend(str)-str)) return getvar(str);
  }
  if (s) push_arg(delete, s);

  return s;
}

// Return length of utf8 char @s fitting in len, writing value into *cc
int getutf8(char *s, int len, int *cc)
{
  wchar_t wc;

  if (len<0) wc = len = 0;
  else if (1>(len = utf8towc(&wc, s, len))) wc = *s, len = 1;
  if (cc) *cc = wc;

  return len;
}

#define WILD_SHORT 1 // else longest match
#define WILD_CASE  2 // case insensitive
#define WILD_ANY   4 // advance through pattern instead of str
// Returns length of str matched by pattern, or -1 if not all pattern consumed
static int wildcard_matchlen(char *str, int len, char *pattern, int plen,
  struct sh_arg *deck, int flags)
{
  struct sh_arg ant = {0};    // stack: of str offsets
  long ss, pp, dd, best = -1;
  int i, j, c, not;

  // Loop through wildcards in pattern.
  for (ss = pp = dd = 0; ;) {
    if ((flags&WILD_ANY) && best!=-1) break;

    // did we consume pattern?
    if (pp==plen) {
      if (ss>best) best = ss;
      if (ss==len || (flags&WILD_SHORT)) break;
    // attempt literal match?
    } else if (dd>=deck->c || pp!=(long)deck->v[dd]) {
      if (ss<len) {
        if (flags&WILD_CASE) {
          c = towupper(getutf8(str+ss, len-ss, &i));
          ss += i;
          i = towupper(getutf8(pattern+pp, pp-plen, &j));
          pp += j;
        } else c = str[ss++], i = pattern[pp++];
        if (c==i) continue;
      }

    // Wildcard chars: |+@!*?()[]
    } else {
      c = pattern[pp++];
      dd++;
      if (c=='?' || ((flags&WILD_ANY) && c=='*')) {
        ss += (i = getutf8(str+ss, len-ss, 0));
        if (i) continue;
      } else if (c=='*') {

        // start with zero length match, don't record consecutive **
        if (dd==1 || pp-2!=(long)deck->v[dd-1] || pattern[pp-2]!='*') {
          arg_add(&ant, (void *)ss);
          arg_add(&ant, 0);
        }

        continue;
      } else if (c == '[') {
        pp += (not = !!strchr("!^", pattern[pp]));
        ss += getutf8(str+ss, len-ss, &c);
        for (i = 0; pp<(long)deck->v[dd]; i = 0) {
          pp += getutf8(pattern+pp, plen-pp, &i);
          if (pattern[pp]=='-') {
            ++pp;
            pp += getutf8(pattern+pp, plen-pp, &j);
            if (not^(i<=c && j>=c)) break;
          } else if (not^(i==c)) break;
        }
        if (i) {
          pp = 1+(long)deck->v[dd++];

          continue;
        }

      // ( preceded by +@!*?

      } else { // TODO ( ) |
        dd++;
        continue;
      }
    }

    // match failure
    if (flags&WILD_ANY) {
      ss = 0;
      if (plen==pp) break;
      continue;
    }

    // pop retry stack or return failure (TODO: seek to next | in paren)
    while (ant.c) {
      if ((c = pattern[(long)deck->v[--dd]])=='*') {
        if (len<(ss = (long)ant.v[ant.c-2]+(long)++ant.v[ant.c-1])) ant.c -= 2;
        else {
          pp = (long)deck->v[dd++]+1;
          break;
        }
      } else if (c == '(') dprintf(2, "TODO: (");
    }

    if (!ant.c) break;
  }
  free (ant.v);

  return best;
}

static int wildcard_match(char *s, char *p, struct sh_arg *deck, int flags)
{
  return wildcard_matchlen(s, strlen(s), p, strlen(p), deck, flags);
}


// TODO: test that * matches ""

// skip to next slash in wildcard path, passing count active ranges.
// start at pattern[off] and deck[*idx], return pattern pos and update *idx
char *wildcard_path(char *pattern, int off, struct sh_arg *deck, int *idx,
  int count)
{
  char *p, *old;
  int i = 0, j = 0;

  // Skip [] and nested () ranges within deck until / or NUL
  for (p = old = pattern+off;; p++) {
    if (!*p) return p;
    while (*p=='/') {
      old = p++;
      if (j && !count) return old;
      j = 0;
    }

    // Got wildcard? Return if start of name if out of count, else skip [] ()
    if (*idx<deck->c && p-pattern == (long)deck->v[*idx]) {
      if (!j++ && !count--) return old;
      ++*idx;
      if (*p=='[') p = pattern+(long)deck->v[(*idx)++];
      else if (*p=='(') while (*++p) if (p-pattern == (long)deck->v[*idx]) {
        ++*idx;
        if (*p == ')') {
          if (!i) break;
          i--;
        } else if (*p == '(') i++;
      }
    }
  }
}

// TODO ** means this directory as well as ones below it, shopt -s globstar

// Filesystem traversal callback
// pass on: filename, portion of deck, portion of pattern,
// input: pattern+offset, deck+offset. Need to update offsets.
int do_wildcard_files(struct dirtree *node)
{
  struct dirtree *nn;
  char *pattern, *patend;
  int lvl, ll = 0, ii = 0, rc;
  struct sh_arg ant;

  // Top level entry has no pattern in it
  if (!node->parent) return DIRTREE_RECURSE;

  // Find active pattern range
  for (nn = node->parent; nn; nn = nn->parent) if (nn->parent) ii++;
  pattern = wildcard_path(TT.wcpat, 0, TT.wcdeck, &ll, ii);
  while (*pattern=='/') pattern++;
  lvl = ll;
  patend = wildcard_path(TT.wcpat, pattern-TT.wcpat, TT.wcdeck, &ll, 1);

  // Don't include . entries unless explicitly asked for them 
  if (*node->name=='.' && *pattern!='.') return 0;

  // Don't descend into non-directory (was called with DIRTREE_SYMFOLLOW)
  if (*patend && !S_ISDIR(node->st.st_mode) && *node->name) return 0;

  // match this filename from pattern to p in deck from lvl to ll
  ant.c = ll-lvl;
  ant.v = TT.wcdeck->v+lvl;
  for (ii = 0; ii<ant.c; ii++) TT.wcdeck->v[lvl+ii] -= pattern-TT.wcpat;
  rc = wildcard_matchlen(node->name, strlen(node->name), pattern,
    patend-pattern, &ant, 0);
  for (ii = 0; ii<ant.c; ii++) TT.wcdeck->v[lvl+ii] += pattern-TT.wcpat;

  // Return failure or save exact match.
  if (rc<0 || node->name[rc]) return 0;
  if (!*patend) return DIRTREE_SAVE;

  // Are there more wildcards to test children against?
  if (TT.wcdeck->c!=ll) return DIRTREE_RECURSE;

  // No more wildcards: check for child and return failure if it isn't there.
  pattern = xmprintf("%s%s", node->name, patend);
  rc = faccessat(dirtree_parentfd(node), pattern, F_OK, AT_SYMLINK_NOFOLLOW);
  free(pattern);
  if (rc) return 0;

  // Save child and self. (Child could be trailing / but only one saved.)
  while (*patend=='/' && patend[1]) patend++;
  node->child = xzalloc(sizeof(struct dirtree)+1+strlen(patend));
  node->child->parent = node;
  strcpy(node->child->name, patend);

  return DIRTREE_SAVE;
}

// Record active wildcard chars in output string
// *new start of string, oo offset into string, deck is found wildcards,
static void collect_wildcards(char *new, long oo, struct sh_arg *deck)
{
  long bracket, *vv;
  char cc = new[oo];

  // Record unescaped/unquoted wildcard metadata for later processing

  if (!deck->c) arg_add(deck, 0);
  vv = (long *)deck->v;

  // vv[0] used for paren level (bottom 16 bits) + bracket start offset<<16

  // at end loop backwards through live wildcards to remove pending unmatched (
  if (!cc) {
    long ii = 0, jj = 65535&*vv, kk;

    for (kk = deck->c; jj;) {
      if (')' == (cc = new[vv[--kk]])) ii++;
      else if ('(' == cc) {
        if (ii) ii--;
        else {
          memmove(vv+kk, vv+kk+1, sizeof(long)*(deck->c-- -kk));
          jj--;
        }
      }
    }
    if (deck->c) memmove(vv, vv+1, sizeof(long)*deck->c--);

    return;
  }

  // Start +( range, or remove first char that isn't wildcard without (
  if (deck->c>1 && vv[deck->c-1] == oo-1 && strchr("+@!*?", new[oo-1])) {
    if (cc == '(') {
      vv[deck->c-1] = oo;
      return;
    } else if (!strchr("*?", new[oo-1])) deck->c--;
  }

  // fall through to add wildcard, popping parentheses stack as necessary
  if (strchr("|+@!*?", cc));
  else if (cc == ')' && (65535&*vv)) --*vv;

  // complete [range], discard wildcards within, add [, fall through to add ]
  else if (cc == ']' && (bracket = *vv>>16)) {

    // don't end range yet for [] or [^]
    if (bracket+1 == oo || (bracket+2 == oo && strchr("!^", new[oo-1]))) return;
    while (deck->c>1 && vv[deck->c-1]>=bracket) deck->c--;
    *vv &= 65535;
    arg_add(deck, (void *)bracket);

  // Not a wildcard
  } else {
    // [ is speculative, don't add to deck yet, just record we saw it
    if (cc == '[' && !(*vv>>16)) *vv = (oo<<16)+(65535&*vv);
    return;
  }

  // add active wildcard location
  arg_add(deck, (void *)oo);
}

// wildcard expand data against filesystem, and add results to arg list
// Note: this wildcard deck has extra argument at start (leftover from parsing)
static void wildcard_add_files(struct sh_arg *arg, char *pattern,
  struct sh_arg *deck, struct arg_list **delete)
{
  struct dirtree *dt;
  char *pp;
  int ll = 0;

  // fast path: when no wildcards, add pattern verbatim
  collect_wildcards("", 0, deck);
  if (!deck->c) return arg_add(arg, pattern);

  // Traverse starting with leading patternless path.
  pp = wildcard_path(TT.wcpat = pattern, 0, TT.wcdeck = deck, &ll, 0);
  pp = (pp==pattern) ? 0 : xstrndup(pattern, pp-pattern);
  dt = dirtree_flagread(pp, DIRTREE_STATLESS|DIRTREE_SYMFOLLOW,
    do_wildcard_files);
  free(pp);
  deck->c = 0;

  // If no match save pattern, else free tree saving each path found.
  if (!dt) return arg_add(arg, pattern);
  while (dt) {
    while (dt->child) dt = dt->child;
    arg_add(arg, dirtree_path(dt, 0));
    do {
      pp = (void *)dt;
      if ((dt = dt->parent)) dt->child = dt->child->next;
      free(pp);
    } while (dt && !dt->child);
  }
// TODO: test .*/../
}

// Copy string until } including escaped }
// if deck collect wildcards, and store terminator at deck->v[deck->c]
char *slashcopy(char *s, char *c, struct sh_arg *deck)
{
  char *ss;
  long ii, jj;

  for (ii = 0; !strchr(c, s[ii]); ii++) if (s[ii] == '\\') ii++;
  ss = xmalloc(ii+1);
  for (ii = jj = 0; !strchr(c, s[jj]); ii++)
    if ('\\'==(ss[ii] = s[jj++])) ss[ii] = s[jj++];
    else if (deck) collect_wildcards(ss, ii, deck);
  ss[ii] = 0;
  if (deck) {
    arg_add(deck, 0);
    deck->v[--deck->c] = (void *)jj;
    collect_wildcards("", 0, deck);
  }

  return ss;
}

#define NO_QUOTE (1<<0)    // quote removal
#define NO_PATH  (1<<1)    // path expansion (wildcards)
#define NO_SPLIT (1<<2)    // word splitting
#define NO_BRACE (1<<3)    // {brace,expansion}
#define NO_TILDE (1<<4)    // ~username/path
#define NO_NULL  (1<<5)    // Expand to "" instead of NULL
#define SEMI_IFS (1<<6)    // Use ' ' instead of IFS to combine $*
// expand str appending to arg using above flag defines, add mallocs to delete
// if ant not null, save wildcard deck there instead of expanding vs filesystem
// returns 0 for success, 1 for error
static int expand_arg_nobrace(struct sh_arg *arg, char *str, unsigned flags,
  struct arg_list **delete, struct sh_arg *ant)
{
  char cc, qq = flags&NO_QUOTE, sep[6], *new = str, *s, *ss, *ifs, *slice;
  int ii = 0, oo = 0, xx, yy, dd, jj, kk, ll, mm;
  struct sh_arg deck = {0};

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
      if (str != new) free(new);
      new = s;
    }
  }

  // parameter/variable expansion and dequoting
  if (!ant) ant = &deck;
  for (; (cc = str[ii++]); str!=new && (new[oo] = 0)) {
    struct sh_arg aa = {0};
    int nosplit = 0;

    // skip literal chars
    if (!strchr("'\"\\$`"+2*(flags&NO_QUOTE), cc)) {
      if (str != new) new[oo] = cc;
      if (!(flags&NO_PATH) && !(qq&1)) collect_wildcards(new, oo, ant);
      oo++;
      continue;
    }

    // allocate snapshot if we just started modifying
    if (str == new) {
      new = xstrdup(new);
      new[oo] = 0;
    }
    ifs = slice = 0;

    // handle escapes and quoting
    if (cc == '\\') {
      if (!(qq&1) || (str[ii] && strchr("\"\\$`", str[ii])))
        new[oo++] = str[ii] ? str[ii++] : cc;
    } else if (cc == '"') qq++;
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
      kk = parse_word(s, 1, 0)-s;
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
          if (!(ll = parse_word(ss, 0, 0)-ss)) ss = 0;
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

    // $VARIABLE expansions

    } else if (cc == '$') {
      cc = *(ss = str+ii++);
      if (cc=='\'') {
        for (s = str+ii; *s != '\''; oo += wcrtomb(new+oo, unescape2(&s, 0),0));
        ii = s-str+1;

        continue;
      } else if (cc=='"' && !(qq&1)) {
        qq++;

        continue;
      } else if (cc == '{') {

        // Skip escapes to find }, parse_word() guarantees ${} terminates
        for (cc = *++ss; str[ii] != '}'; ii++) if (str[ii]=='\\') ii++;
        ii++;

        if (cc == '}') ifs = (void *)1;
        else if (strchr("#!", cc)) ss++;
        jj = varend(ss)-ss;
        if (!jj) while (isdigit(ss[jj])) jj++;
        if (!jj && strchr("#$!_*", *ss)) jj++;
        // parameter or operator? Maybe not a prefix: ${#-} vs ${#-x}
        if (!jj && strchr("-?@", *ss)) if (ss[++jj]!='}' && ss[-1]!='{') ss--;
        slice = ss+jj;        // start of :operation

        if (!jj) {
          ifs = (void *)1;
          // literal ${#} or ${!} wasn't a prefix
          if (strchr("#!", cc)) ifs = getvar_special(--ss, 1, &kk, delete);
        } else if (ss[-1]=='{'); // not prefix, fall through
        else if (cc == '#') {  // TODO ${#x[@]}
          dd = !!strchr("@*", *ss);  // For ${#@} or ${#*} do normal ${#}
          ifs = getvar_special(ss-dd, jj, &kk, delete) ? : "";
          if (!dd) push_arg(delete, ifs = xmprintf("%zu", strlen(ifs)));
        // ${!@} ${!@Q} ${!x} ${!x@} ${!x@Q} ${!x#} ${!x[} ${!x[*]}
        } else if (cc == '!') {  // TODO: ${var[@]} array

          // special case: normal varname followed by @} or *} = prefix list
          if (ss[jj] == '*' || (ss[jj] == '@' && !isalpha(ss[jj+1]))) {
            for (slice++, kk = 0; kk<TT.varslen; kk++)
              if (!strncmp(s = TT.vars[kk].str, ss, jj))
                arg_add(&aa, push_arg(delete, s = xstrndup(s, stridx(s, '='))));
            if (aa.c) push_arg(delete, aa.v);

          // else dereference to get new varname, discarding if none, check err
          } else {
            // First expansion
            if (strchr("@*", *ss)) { // special case ${!*}/${!@}
              expand_arg_nobrace(&aa, "\"$*\"", NO_PATH|NO_SPLIT, delete, 0);
              ifs = *aa.v;
              free(aa.v);
              memset(&aa, 0, sizeof(aa));
              jj = 1;
            } else ifs = getvar_special(ss, jj, &jj, delete);
            slice = ss+jj;

            // Second expansion
            if (!jj) ifs = (void *)1;
            else if (ifs && *(ss = ifs)) {
              if (strchr("@*", cc)) {
                aa.c = TT.cc->arg.c-1;
                aa.v = TT.cc->arg.v+1;
                jj = 1;
              } else ifs = getvar_special(ifs, strlen(ifs), &jj, delete);
              if (ss && ss[jj]) {
                ifs = (void *)1;
                slice = ss+strlen(ss);
              }
            }
          }
        }

        // Substitution error?
        if (ifs == (void *)1) {
barf:
          if (!(((unsigned long)ifs)>>1)) ifs = "bad substitution";
          error_msg("%.*s: %s", (int)(slice-ss), ss, ifs);
          goto fail;
        }
      } else jj = 1;

      // Resolve unprefixed variables
      if (strchr("{$", ss[-1])) {
        if (strchr("@*", cc)) {
          aa.c = TT.cc->arg.c-1;
          aa.v = TT.cc->arg.v+1;
        } else {
          ifs = getvar_special(ss, jj, &jj, delete);
          if (!jj) {
            if (ss[-1] == '{') goto barf;
            new[oo++] = '$';
            ii--;
            continue;
          } else if (ss[-1] != '{') ii += jj-1;
        }
      }
    }

    // combine before/ifs/after sections & split words on $IFS in ifs
    // keep oo bytes of str before (already parsed)
    // insert ifs (active for wildcards+splitting)
    // keep str+ii after (still to parse)

    // Fetch separator to glue string back together with
    *sep = 0;
    if (((qq&1) && cc=='*') || (flags&NO_SPLIT)) {
      wchar_t wc;

      nosplit++;
      if (flags&SEMI_IFS) strcpy(sep, " ");
// TODO what if separator is bigger? Need to grab 1 column of combining chars
      else if (0<(dd = utf8towc(&wc, TT.ifs, 4)))
        sprintf(sep, "%.*s", dd, TT.ifs);
    }

    // when aa proceed through entries until NULL, else process ifs once
    mm = yy = 0;
    do {

      // get next argument
      if (aa.c) ifs = aa.v[mm++] ? : "";

      // Are we performing surgery on this argument?
      if (slice && *slice != '}') {
        dd = slice[xx = (*slice == ':')];
        if (!ifs || (xx && !*ifs)) {
          if (strchr("-?=", dd)) { // - use default = assign default ? error
            push_arg(delete, ifs = slashcopy(slice+xx+1, "}", 0));
            if (dd == '?' || (dd == '=' &&
              !(setvar(s = xmprintf("%.*s=%s", (int)(slice-ss), ss, ifs)))))
                goto barf;
          }
        } else if (dd == '-'); // NOP when ifs not empty
        // use alternate value
        else if (dd == '+')
          push_arg(delete, ifs = slashcopy(slice+xx+1, "}", 0));
        else if (xx) { // ${x::}
          long long la, lb, lc;

// TODO don't redo math in loop
          ss = slice+1;
          la = do_math(&s);
          if (s && *s == ':') {
            s++;
            lb = do_math(&s);
          } else lb = LLONG_MAX;
          if (s && *s != '}') {
            error_msg("%.*s: bad '%c'", (int)(slice-ss), ss, *s);
            s = 0;
          }
          if (!s) goto fail;

          // This isn't quite what bash does, but close enough.
          if (!(lc = aa.c)) lc = strlen(ifs);
          else if (!la && !yy && strchr("@*", slice[1])) {
            aa.v--; // ${*:0} shows $0 even though default is 1-indexed
            aa.c++;
            yy++;
          }
          if (la<0 && (la += lc)<0) continue;
          if (lb<0) lb = lc+lb-la;
          if (aa.c) {
            if (mm<la || mm>=la+lb) continue;
          } else if (la>=lc || lb<0) ifs = "";
          else if (la+lb>=lc) ifs += la;
          else if (!*delete || ifs != (*delete)->arg)
            push_arg(delete, ifs = xmprintf("%.*s", (int)lb, ifs+la));
          else {
            for (dd = 0; dd<lb ; dd++) if (!(ifs[dd] = ifs[dd+la])) break;
            ifs[dd] = 0;
          }
        } else if (strchr("#%^,", *slice)) {
          struct sh_arg wild = {0};
          char buf[8];

          s = slashcopy(slice+(xx = slice[1]==*slice)+1, "}", &wild);

          // ${x^pat} ${x^^pat} uppercase ${x,} ${x,,} lowercase (no pat = ?)
          if (strchr("^,", *slice)) {
            for (ss = ifs; *ss; ss += dd) {
              dd = getutf8(ss, 4, &jj);
              if (!*s || 0<wildcard_match(ss, s, &wild, WILD_ANY)) {
                ll = ((*slice=='^') ? towupper : towlower)(jj);

                // Of COURSE unicode case switch can change utf8 encoding length
                // Lower case U+0069 becomes u+0130 in turkish.
                // Greek U+0390 becomes 3 characters TODO test this
                if (ll != jj) {
                  yy = ss-ifs;
                  if (!*delete || (*delete)->arg!=ifs)
                    push_arg(delete, ifs = xstrdup(ifs));
                  if (dd != (ll = wctoutf8(buf, ll))) {
                    if (dd<ll)
                      ifs = (*delete)->arg = xrealloc(ifs, strlen(ifs)+1+dd-ll);
                    memmove(ifs+yy+dd-ll, ifs+yy+ll, strlen(ifs+yy+ll)+1);
                  }
                  memcpy(ss = ifs+yy, buf, dd = ll);
                }
              }
              if (!xx) break;
            }
          // ${x#y} remove shortest prefix ${x##y} remove longest prefix
          } else if (*slice=='#') {
            if (0<(dd = wildcard_match(ifs, s, &wild, WILD_SHORT*!xx)))
              ifs += dd;
          // ${x%y} ${x%%y} suffix
          } else if (*slice=='%') {
            for (ss = ifs+strlen(ifs), yy = -1; ss>=ifs; ss--) {
              if (0<(dd = wildcard_match(ss, s, &wild, WILD_SHORT*xx))&&!ss[dd])
              {
                yy = ss-ifs;
                if (!xx) break;
              }
            }

            if (yy != -1) {
              if (*delete && (*delete)->arg==ifs) ifs[yy] = 0;
              else push_arg(delete, ifs = xstrndup(ifs, yy));
            }
          }
          free(s);
          free(wild.v);

        // ${x/pat/sub} substitute ${x//pat/sub} global ${x/#pat/sub} begin
        // ${x/%pat/sub} end ${x/pat} delete pat (x can be @ or *)
        } else if (*slice=='/') {
          struct sh_arg wild = {0};

          s = slashcopy(ss = slice+(xx = !!strchr("/#%", slice[1]))+1, "/}",
            &wild);
          ss += (long)wild.v[wild.c];
          ss = (*ss == '/') ? slashcopy(ss+1, "}", 0) : 0;
          jj = ss ? strlen(ss) : 0;
          ll = 0;
          for (ll = 0; ifs[ll];) {
            // TODO nocasematch option
            if (0<(dd = wildcard_match(ifs+ll, s, &wild, 0))) {
              char *bird = 0;

              if (slice[1]=='%' && ifs[ll+dd]) {
                ll++;
                continue;
              }
              if (*delete && (*delete)->arg==ifs) {
                if (jj==dd) memcpy(ifs+ll, ss, jj);
                else if (jj<dd) sprintf(ifs+ll, "%s%s", ss, ifs+ll+dd);
                else bird = ifs;
              } else bird = (void *)1;
              if (bird) {
                ifs = xmprintf("%.*s%s%s", ll, ifs, ss ? : "", ifs+ll+dd);
                if (bird != (void *)1) {
                  free(bird);
                  (*delete)->arg = ifs;
                } else push_arg(delete, ifs);
              }
              if (slice[1]!='/') break;
            } else ll++;
            if (slice[1]=='#') break;
          }

// ${x@QEPAa} Q=$'blah' E=blah without the $'' wrap, P=expand as $PS1
//   A=declare that recreates var a=attribute flags
//   x can be @*
//      } else if (*slice=='@') {

// TODO test x can be @ or *
        } else {
// TODO test ${-abc} as error
          ifs = slice;
          goto barf;
        }

// TODO: $((a=42)) can change var, affect lifetime
// must replace ifs AND any previous output arg[] within pointer strlen()
// also x=;echo $x${x:=4}$x
      }

      // Nothing left to do?
      if (!ifs) break;
      if (!*ifs && !qq) continue;

      // loop within current ifs checking region to split words
      do {

        // find end of (split) word
        if ((qq&1) || nosplit) ss = ifs+strlen(ifs);
        else for (ss = ifs; *ss; ss += kk) if (utf8chr(ss, TT.ifs, &kk)) break;

        // when no prefix, not splitting, no suffix: use existing memory
        if (!oo && !*ss && !((mm==aa.c) ? str[ii] : nosplit)) {
          if (qq || ss!=ifs) {
            if (!(flags&NO_PATH))
              for (jj = 0; ifs[jj]; jj++) collect_wildcards(ifs, jj, ant);
            wildcard_add_files(arg, ifs, &deck, delete);
          }
          continue;
        }

        // resize allocation and copy next chunk of IFS-free data
        jj = (mm == aa.c) && !*ss;
        new = xrealloc(new, oo + (ss-ifs) + ((nosplit&!jj) ? strlen(sep) : 0) +
                       (jj ? strlen(str+ii) : 0) + 1);
        dd = sprintf(new + oo, "%.*s%s", (int)(ss-ifs), ifs,
          (nosplit&!jj) ? sep : "");
        if (flags&NO_PATH) oo += dd;
        else while (dd--) collect_wildcards(new, oo++, ant);
        if (jj) break;

        // If splitting, keep quoted, non-blank, or non-whitespace separator
        if (!nosplit) {
          if (qq || *new || *ss) {
            push_arg(delete, new = xrealloc(new, strlen(new)+1));
            wildcard_add_files(arg, new, &deck, delete);
            new = xstrdup(str+ii);
          }
          qq &= 1;
          oo = 0;
        }

        // Skip trailing seperator (combining whitespace)
        while ((jj = utf8chr(ss, TT.ifs, &ll))) {
          ss += ll;
          if (!iswspace(jj)) break;
        }
      } while (*(ifs = ss));
    } while (!(mm == aa.c));
  }

// TODO globbing * ? [] +() happens after variable resolution

// TODO test word splitting completely eliminating argument when no non-$IFS data left
// wordexp keeps pattern when no matches

// TODO test NO_SPLIT cares about IFS, see also trailing \n

  // Record result.
  if (*new || qq) {
    if (str != new) push_arg(delete, new);
    wildcard_add_files(arg, new, &deck, delete);
    new = 0;
  }

  // return success after freeing
  arg = 0;

fail:
  if (str != new) free(new);
  free(deck.v);
  if (ant!=&deck && ant->v) collect_wildcards("", 0, ant);

  return !!arg;
}

struct sh_brace {
  struct sh_brace *next, *prev, *stack;
  int active, cnt, idx, commas[];
};

static int brace_end(struct sh_brace *bb)
{
  return bb->commas[(bb->cnt<0 ? 0 : bb->cnt)+1];
}

// expand braces (ala {a,b,c}) and call expand_arg_nobrace() each permutation
static int expand_arg(struct sh_arg *arg, char *old, unsigned flags,
  struct arg_list **delete)
{
  struct sh_brace *bb = 0, *blist = 0, *bstk, *bnext;
  int i, j, k, x;
  char *s, *ss;

  // collect brace spans
  if ((TT.options&OPT_B) && !(flags&NO_BRACE)) for (i = 0; ; i++) {
    // skip quoted/escaped text
    while ((s = parse_word(old+i, 1, 0)) != old+i) i += s-(old+i);
    // stop at end of string if we haven't got any more open braces
    if (!bb && !old[i]) break;
    // end a brace?
    if (bb && (!old[i] || old[i] == '}')) {
      bb->active = bb->commas[bb->cnt+1] = i;
      // pop brace from bb into bnext
      for (bnext = bb; bb && bb->active; bb = (bb==blist) ? 0 : bb->prev);
      // Is this a .. span?
      j = 1+*bnext->commas;
      if (old[i] && !bnext->cnt && i-j>=4) {
        // a..z span? Single digit numbers handled here too. TODO: utf8
        if (old[j+1]=='.' && old[j+2]=='.') {
          bnext->commas[2] = old[j];
          bnext->commas[3] = old[j+3];
          k = 0;
          if (old[j+4]=='}' ||
            (sscanf(old+j+4, "..%u}%n", bnext->commas+4, &k) && k))
              bnext->cnt = -1;
        }
        // 3..11 numeric span?
        if (!bnext->cnt) {
          for (k=0, j = 1+*bnext->commas; k<3; k++, j += x)
            if (!sscanf(old+j, "..%u%n"+2*!k, bnext->commas+2+k, &x)) break;
          if (old[j] == '}') bnext->cnt = -2;
        }
        // Increment goes in the right direction by at least 1
        if (bnext->cnt) {
          if (!bnext->commas[4]) bnext->commas[4] = 1;
          if ((bnext->commas[3]-bnext->commas[2]>0) != (bnext->commas[4]>0))
            bnext->commas[4] *= -1;
        }
      }
      // discard unterminated span, or commaless span that wasn't x..y
      if (!old[i] || !bnext->cnt)
        free(dlist_pop((blist == bnext) ? &blist : &bnext));
    // starting brace
    } else if (old[i] == '{') {
      dlist_add_nomalloc((void *)&blist,
        (void *)(bb = xzalloc(sizeof(struct sh_brace)+34*4)));
      bb->commas[0] = i;
    // no active span?
    } else if (!bb) continue;
    // add a comma to current span
    else if (bb && old[i] == ',') {
      if (bb->cnt && !(bb->cnt&31)) {
        dlist_lpop(&blist);
        dlist_add_nomalloc((void *)&blist,
          (void *)(bb = xrealloc(bb, sizeof(struct sh_brace)+(bb->cnt+34)*4)));
      }
      bb->commas[++bb->cnt] = i;
    }
  }

// TODO NOSPLIT with braces? (Collate with spaces?)
  // If none, pass on verbatim
  if (!blist) return expand_arg_nobrace(arg, old, flags, delete, 0);

  // enclose entire range in top level brace.
  (bstk = xzalloc(sizeof(struct sh_brace)+8))->commas[1] = strlen(old)+1;
  bstk->commas[0] = -1;

  // loop through each combination
  for (;;) {

    // Brace expansion can't be longer than original string. Keep start to {
    s = ss = xmalloc(bstk->commas[1]);

    // Append output from active braces to string
    for (bb = blist; bb; bb = (bnext == blist) ? 0 : bnext) {

      // If this brace already tip of stack, pop it. (We'll re-add in a moment.)
      if (bstk == bb) bstk = bstk->stack;
      // if bb is within bstk, save prefix text from bstk's "," to bb's "{"
      if (brace_end(bstk)>bb->commas[0]) {
        i = bstk->commas[bstk->idx]+1;
        s = stpncpy(s, old+i, bb->commas[0]-i);
      }
      else bstk = bstk->stack; // bb past bstk so done with old bstk, pop it
      // push self onto stack as active
      bb->stack = bstk;
      bb->active = 1;
      bstk = bnext = bb;

      // Find next active range: skip inactive spans from earlier/later commas
      while ((bnext = (bnext->next==blist) ? 0 : bnext->next)) {

        // past end of this brace (always true for a..b ranges)
        if ((i = bnext->commas[0])>brace_end(bb)) break;

        // in this brace but not this section
        if (i<bb->commas[bb->idx] || i>bb->commas[bb->idx+1]) {
          bnext->active = 0;
          bnext->stack = 0;

        // in this section
        } else break;
      }

      // is next span past this range?
      if (!bnext || bb->cnt<0 || bnext->commas[0]>bb->commas[bb->idx+1]) {

        // output uninterrupted span
        if (bb->cnt<0) {
          k = bb->commas[2]+bb->commas[4]*bb->idx;
          s += sprintf(s, (bb->cnt==-1) ? "\\%c"+!ispunct(k) : "%d", k);
        } else {
          i = bb->commas[bstk->idx]+1;
          s = stpncpy(s, old+i, bb->commas[bb->idx+1]-i);
        }

        // While not sibling, output tail and pop
        while (!bnext || bnext->commas[0]>brace_end(bstk)) {
          if (!(bb = bstk->stack)) break;
          i = brace_end(bstk)+1; // start of span
          j = bb->commas[bb->idx+1]; // enclosing comma span (can't be a..b)

          while (bnext) {
            if (bnext->commas[0]<j) {
              j = bnext->commas[0];// sibling
              break;
            } else if (brace_end(bb)>bnext->commas[0])
              bnext = (bnext->next == blist) ? 0 : bnext->next;
            else break;
          }
          s = stpncpy(s, old+i, j-i);

          // if next is sibling but parent _not_ a sibling, don't pop
          if (bnext && bnext->commas[0]<brace_end(bb)) break;
          bstk = bb;
        }
      }
    }

    // Save result, aborting on expand error
    if (expand_arg_nobrace(arg, push_arg(delete, ss), flags, delete, 0)) {
      llist_traverse(blist, free);

      return 1;
    }

    // increment
    for (bb = blist->prev; bb; bb = (bb == blist) ? 0 : bb->prev) {
      if (!bb->stack) continue;
      else if (bb->cnt<0) {
        if (abs(bb->commas[2]-bb->commas[3]) < abs(++bb->idx*bb->commas[4]))
          bb->idx = 0;
        else break;
      } else if (++bb->idx > bb->cnt) bb->idx = 0;
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
  struct sh_arg arg = {0};
  char *s = 0;

  if (!expand_arg(&arg, new, flags|NO_PATH|NO_SPLIT, del))
    if (!(s = *arg.v) && (flags&(SEMI_IFS|NO_NULL))) s = "";
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

    if (!(sss = pl->arg->v[pl->arg->c])) sss = ";"+!end->next;
    if (j) ss = stpcpy(ss, sss);
    else len += strlen(sss);

// TODO test output with case and function
// TODO add HERE documents back in
    if (j) return s;
    s = ss = xmalloc(len+1);
  }
}

// Expand arguments and perform redirections. Return new process object with
// expanded args. This can be called from command or block context.
static struct sh_process *expand_redir(struct sh_arg *arg, int skip, int *urd)
{
  struct sh_process *pp;
  char *s = s, *ss, *sss, *cv = 0;
  int j, to, from, here = 0;

  TT.hfd = 10;

  pp = xzalloc(sizeof(struct sh_process));
  pp->urd = urd;
  pp->raw = arg;

  // When we redirect, we copy each displaced filehandle to restore it later.

  // Expand arguments and perform redirections
  for (j = skip; j<arg->c; j++) {
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
    if (strncmp(ss, "<<", 2) && ss[2] != '<') {
      struct sh_arg tmp = {0};

      if (!expand_arg(&tmp, sss, 0, &pp->delete) && tmp.c == 1) sss = *tmp.v;
      else {
        if (tmp.c > 1) error_msg("%s: ambiguous redirect", sss);
        s = 0;
      }
      free(tmp.v);
      if (!s) break;
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
          if (!(ss = expand_one_arg(sss, 0, 0))) {
            s = 0;
            break;
          }
          len = strlen(ss);
          if (len != writeall(from, ss, len)) bad++;
          if (ss != sss) free(ss);
        } else {
          struct sh_arg *hh = arg+here++;

          for (i = 0; i<hh->c; i++) {
            ss = hh->v[i];
            sss = 0;
// TODO audit this ala man page
            // expand_parameter, commands, and arithmetic
            if (x && !(ss = sss = expand_one_arg(ss, ~SEMI_IFS, 0))) {
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
        if (!strcmp(ss, ">") && (TT.options&OPT_C)) {
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
// TODO: why?
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
  struct sh_arg env = {0};
  int envlen, jj = 0, ll;
  struct sh_process *pp;
  struct arg_list *delete = 0;
  struct toy_list *tl;

  // Count leading variable assignments
  for (envlen = 0; envlen<arg->c; envlen++) {
    s = varend(arg->v[envlen]);
    if (s == arg->v[envlen] || *s != '=') break;
  }

  // perform assignments locally if there's no command
  if (envlen == arg->c) {
    while (jj<envlen) {
      if (!(s = expand_one_arg(arg->v[jj], SEMI_IFS, 0))) break;
      setvar((s == arg->v[jj++]) ? xstrdup(s) : s);
    }
    if (jj == envlen) setvarval("_", "");

  // assign leading environment variables (if any) in temp environ copy
  } else if (envlen) {
    while (environ[env.c]) env.c++;
    memcpy(env.v = xmalloc(sizeof(char *)*(env.c+33)), environ,
      sizeof(char *)*(env.c+1));
    for (; jj<envlen; jj++) {
      if (!(sss = expand_one_arg(arg->v[jj], SEMI_IFS, &delete))) break;
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

  // return early if error or assignment only
  if (envlen == arg->c || jj != envlen) {
    pp = xzalloc(sizeof(struct sh_process));
    pp->exit = jj != envlen;

    goto out;
  }

  // expand arguments and perform redirects
  pp = expand_redir(arg, envlen, 0);

  // Do nothing if nothing to do
  if (pp->exit || !pp->arg.v);
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
    memset(&TT, 0, offsetof(struct sh_data, ifs));

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
  } else if (-1==(pp->pid = xpopen_setup(pp->arg.v, 0, sh_exec)))
    perror_msg("%s: vfork", *pp->arg.v);

  // Restore environment variables
  environ = old;
  free(env.v);

  if (pp->arg.c) setvarval("_", pp->arg.v[pp->arg.c-1]);
  // cleanup process
  unredirect(pp->urd);
out:
  llist_traverse(delete, llist_free_arg);

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

// Append a new pipeline to function, returning pipeline and pipeline's arg
static struct sh_pipeline *add_pl(struct sh_function *sp, struct sh_arg **arg)
{
  struct sh_pipeline *pl = xzalloc(sizeof(struct sh_pipeline));

  *arg = pl->arg;
  dlist_add_nomalloc((void *)&sp->pipeline, (void *)pl);

  return pl->end = pl;
}

// Add a line of shell script to a shell function. Returns 0 if finished,
// 1 to request another line of input (> prompt), -1 for syntax err
static int parse_line(char *line, struct sh_function *sp)
{
  char *start = line, *delete = 0, *end, *s, *ex, done = 0,
    *tails[] = {"fi", "done", "esac", "}", "]]", ")", 0};
  struct sh_pipeline *pl = sp->pipeline ? sp->pipeline->prev : 0, *pl2, *pl3;
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

    // Nope, new segment if not self-managing type
    } else if (pl->type < 128) pl = 0;
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
    if ((end = parse_word(start, 0, 0)) == (void *)1) goto flush;

// dprintf(2, "word[%ld]=%.*s (%s)\n", end ? end-start : 0, (int)(end ? end-start : 0), start, ex);

    // Is this a new pipeline segment?
    if (!pl) pl = add_pl(sp, &arg);

    // Do we need to request another line to finish word (find ending quote)?
    if (!end) {
      // Save unparsed bit of this line, we'll need to re-parse it.
      arg_add(arg, xstrndup(start, strlen(start)));
      arg->c = -arg->c;
      free(delete);

      return 1;
    }

    // Ok, we have a word. What does it _mean_?

    // case/esac parsing is weird (unbalanced parentheses!), handle first
    i = ex && !strcmp(ex, "esac") && (pl->type || (*start==';' && end-start>1));
    if (i) {
      // Premature EOL in type 1 (case x\nin) or 2 (at start or after ;;) is ok
      if (end == start) {
        if (pl->type==128 && arg->c==2) break;  // case x\nin
        if (pl->type==129 && (!arg->c || (arg->c==1 && **arg->v==';'))) break;
        s = "newline";
        goto flush;
      }

      // type 0 means just got ;; so start new type 2
      if (!pl->type) {
        // catch "echo | ;;" errors
        if (arg->v && arg->v[arg->c] && strcmp(arg->v[arg->c], "&")) goto flush;
        if (!arg->c) {
          if (pl->prev->type == 2) {
            // Add a call to "true" between empty ) ;;
            arg_add(arg, xstrdup(":"));
            pl = add_pl(sp, &arg);
          }
          pl->type = 129;
        } else {
          // check for here documents
          pl->count = -1;
          continue;
        }
      }

    // Did we hit end of line or ) outside a function declaration?
    // ) is only saved at start of a statement, ends current statement
    } else if (end == start || (arg->c && *start == ')' && pl->type!='f')) {
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

      continue;
    }

    // Save word and check for flow control
    arg_add(arg, s = xstrndup(start, end-start));
    start = end;

    // Second half of case/esac parsing
    if (i) {
      // type 1 (128): case x [\n] in
      if (pl->type==128) {
        if (arg->c==2 && strchr("()|;&", *s)) goto flush;
        if (arg->c==3) {
          if (strcmp(s, "in")) goto flush;
          pl->type = 1;
          (pl = add_pl(sp, &arg))->type = 129;
        }

        continue;

      // type 2 (129): [;;] [(] pattern [|pattern...] )
      } else {

        // can't start with line break or ";;" or "case ? in ;;" without ")"
        if (*s==';') {
          if (arg->c>1 || (arg->c==1 && pl->prev->type==1)) goto flush;
        } else pl->type = 2;
        i = arg->c - (**arg->v==';' && arg->v[0][1]);
        if (i==1 && !strcmp(s, "esac")) {
          // esac right after "in" or ";;" ends block, fall through
          if (arg->c>1) {
            arg->v[1] = 0;
            pl = add_pl(sp, &arg);
            arg_add(arg, s);
          } else pl->type = 0;
        } else {
          if (arg->c>1) i -= *arg->v[1]=='(';
          if (i>0 && ((i&1)==!!strchr("|)", *s) || strchr(";(", *s)))
            goto flush;
          if (*s=='&' || !strcmp(s, "||")) goto flush;
          if (*s==')') pl = add_pl(sp, &arg);

          continue;
        }
      }
    }

    // is it a line break token?
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
      } else if (*s == ';') goto flush;

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
        dlist_add(&sp->expect, "}");
        dlist_add(&sp->expect, 0);
        dlist_add(&sp->expect, "{");

        continue;
      }

    // a for/select must have at least one additional argument on same line
    } else if (ex && !memcmp(ex, "do\0A", 4)) {

      // Sanity check and break the segment
      if (strncmp(s, "((", 2) && *varend(s)) goto flush;
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

    // for/select/case require var name on same line, can't break segment yet
    if (!strcmp(s, "for") || !strcmp(s, "select") || !strcmp(s, "case")) {
// TODO why !pl->type here
      if (!pl->type) pl->type = (*s == 'c') ? 128 : 1;
      dlist_add(&sp->expect, (*s == 'c') ? "esac" : "do\0A");

      continue;
    }

    end = 0;
    if (!strcmp(s, "if")) end = "then";
    else if (!strcmp(s, "while") || !strcmp(s, "until")) end = "do\0B";
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
      struct sh_arg *aa = pl->prev->arg;

      // can't "if | then" or "while && do", only ; & or newline works
      if (aa->v[aa->c] && strcmp(aa->v[aa->c], "&")) goto flush;

      // consume word, record block end location in earlier !0 type blocks
      free(dlist_lpop(&sp->expect));
      if (3 == (pl->type = anystr(s, tails) ? 3 : 2)) {
        for (i = 0, pl2 = pl3 = pl; (pl2 = pl2->prev);) {
          if (pl2->type == 3) i++;
          else if (pl2->type) {
            if (!i) {
              if (pl2->type == 2) {
                pl2->end = pl3;
                pl3 = pl2;
              } else pl2->end = pl;
            }
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

// Stack of nested if/else/fi and for/do/done contexts.
struct blockstack {
  struct blockstack *next;
  struct sh_pipeline *start, *middle;
  struct sh_process *pp;       // list of processes piping in to us
  int run, loop, *urd, pout;
  struct sh_arg farg;          // for/select arg stack, case wildcard deck
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
  free(blk->farg.v);
  free(llist_pop(blist));

  return pl;
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
        pp += snprintf(pp, len, "%ld", TT.cc->lineno);
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

  if (TT.options&OPT_x) {
    char *s = pl2str(pl);

    do_prompt(getvar("PS4"));
    dprintf(2, "%s\n", s);
    free(s);
  }

  // iterate through pipeline segments
  while (pl) {
    char *ctl = pl->end->arg->v[pl->end->arg->c], **vv,
      *s = *pl->arg->v, *ss = pl->arg->v[1];

    // Skip disabled blocks, handle pipes
    if (pl->type<2) {
      if (blk && !blk->run) {
        pl = pl->end->next;
        continue;
      }
      if (pipe_segments(ctl, pipes, &urd)) break;
    }

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

      // What flow control statement is this?

      // {/} if/then/elif/else/fi, while until/do/done - no special handling

      // for/select/do/done: populate blk->farg with expanded arguments (if any)
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
        // in without LIST. (This expansion can't return error.)
        } else expand_arg(&blk->farg, "\"$@\"", 0, &blk->fdelete);

        // TODO: ls -C style output
        if (*s == 's') for (i = 0; i<blk->farg.c; i++)
          dprintf(2, "%ld) %s\n", i+1, blk->farg.v[i]);

      // TODO: bash man page says it performs <(process substituion) here?!?
      } else if (!strcmp(s, "case"))
        if (!(blk->fvar = expand_one_arg(ss, NO_NULL, &blk->fdelete))) break;

// TODO [[/]] ((/)) function/}

    // gearshift from block start to block body (end of flow control test)
    } else if (pl->type == 2) {
      int match, err;

      blk->middle = pl;

      // ;; end, ;& continue through next block, ;;& test next block
      if (!strcmp(*blk->start->arg->v, "case")) {
        if (!strcmp(s, ";;")) {
          while (pl->type!=3) pl = pl->end;
          continue;
        } else if (strcmp(s, ";&")) {
          struct sh_arg arg = {0}, arg2 = {0};

          for (err = 0, vv = 0;;) {
            if (!vv) {
              vv = pl->arg->v + (**pl->arg->v == ';');
              if (!*vv) {
                pl = pl->next; // TODO syntax err if not type==3, catch above
                break;
              } else vv += **vv == '(';
            }
            arg.c = 0;
            if ((err = expand_arg_nobrace(&arg, *vv++, NO_SPLIT, &blk->fdelete,
              &arg2))) break;
            s = arg.c ? *arg.v : "";
            match = wildcard_match(blk->fvar, s, &arg2, 0);
            if (match>=0 && !s[match]) break;
            else if (**vv++ == ')') {
              vv = 0;
              if ((pl = pl->end)->type!=2) break;
            }
          }
          free(arg.v);
          free(arg2.v);
          if (err) break;
          if (pl->type==3) continue;
        }

      // Handle if/else/elif statement
      } else if (!strcmp(s, "then")) blk->run = blk->run && !toys.exitval;
      else if (!strcmp(s, "else") || !strcmp(s, "elif")) blk->run = !blk->run;

      // Loop
      else if (!strcmp(s, "do")) {
        ss = *blk->start->arg->v;
        if (!strcmp(ss, "while")) blk->run = blk->run && !toys.exitval;
        else if (!strcmp(ss, "until")) blk->run = blk->run && toys.exitval;
        else if (!strcmp(ss, "select")) {
          do_prompt(getvar("PS3"));
// TODO: ctrl-c not breaking out of this?
          if (!(ss = xgetline(stdin))) {
            pl = pop_block(&blk, pipes);
            printf("\n");
          } else if (!*ss) {
            pl = blk->start;
            continue;
          } else {
            match = atoi(ss);
            setvarval(blk->fvar, (match<1 || match>blk->farg.c)
              ? "" : blk->farg.v[match-1]);
          }
        } else if (blk->loop >= blk->farg.c) pl = pop_block(&blk, pipes);
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
    if (!pl->type || pl->type == 3) {
      while (ctl && !strcmp(ctl, toys.exitval ? "&&" : "||")) {
        if ((pl = pl->next)->type) pl = pl->end;
        ctl = pl->arg->v[pl->arg->c];
      }
    }
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
// TODO else error?
  free_function(&scratch);

  return toys.exitval;
}

// set variable
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

FILE *fpathopen(char *name)
{
  struct string_list *sl = 0;
  FILE *f = fopen(name, "r");
  char *pp = getvar("PATH") ? : _PATH_DEFPATH;

  if (!f) {
    for (sl = find_in_path(pp, name); sl; free(llist_pop(&sl)))
      if ((f = fopen(sl->str, "r"))) break;
    if (sl) llist_traverse(sl, free);
  }

  return f;
}

// get line with command history
char *prompt_getline(FILE *ff, int prompt)
{
  char *new, ps[16];

// TODO line editing/history, should set $COLUMNS $LINES and sigwinch update
  errno = 0;
  if (!ff && prompt) {
    sprintf(ps, "PS%d", prompt);
    do_prompt(getvar(ps));
  }
  do if ((new = xgetline(ff ? : stdin))) return new;
  while (errno == EINTR);
//  TODO: after first EINTR returns closed?
// TODO: ctrl-z during script read having already read partial line,
// SIGSTOP and SIGTSTP need need SA_RESTART, but child proc should stop
// TODO if (!isspace(*new)) add_to_history(line);

  return 0;
}

// Read script input and execute lines, with or without prompts
int do_source(char *name, FILE *ff)
{
  struct sh_callstack *cc = xzalloc(sizeof(struct sh_callstack));
  int more = 0;
  char *new;

  cc->next = TT.cc;
  cc->arg.v = toys.optargs;
  cc->arg.c = toys.optc;
  TT.cc = cc;

  do {
    new = prompt_getline(ff, more+1);
    if (!TT.cc->lineno++ && new && !memcmp(new, "\177ELF", 4)) {
      error_msg("'%s' is ELF", name);
      free(new);

      break;
    }

    // TODO: source <(echo 'echo hello\') vs source <(echo -n 'echo hello\')
    // prints "hello" vs "hello\"

    // returns 0 if line consumed, command if it needs more data
    more = parse_line(new ? : " ", &cc->scratch);
    if (more==1) {
      if (!new && !ff) syntax_err("unexpected end of file");
    } else {
      if (!more) run_function(cc->scratch.pipeline);
      free_function(&cc->scratch);
      more = 0;
    }
    free(new);
  } while(new);

  if (ff) fclose(ff);
  TT.cc = TT.cc->next;
  free_function(&cc->scratch);
  llist_traverse(cc->delete, llist_free_arg);
  free(cc);

  return more;
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
  initlocal("PS3", "#? ");
  initlocal("PS4", "+ ");

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
  environ[to++] = 0;
  toys.envc = to;

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
  do_source("", fdopen(254, "r"));

  xexit();
}

void sh_main(void)
{
  char *cc = 0;
  FILE *ff;

  signal(SIGPIPE, SIG_IGN);
  TT.options = OPT_B;

  TT.pid = getpid();
  TT.SECONDS = time(0);

  // TODO euid stuff?
  // TODO login shell?
  // TODO read profile, read rc

  // if (!FLAG(noprofile)) { }

  // If not reentering, figure out if this is an interactive shell.
  if (toys.stacktop) {
    cc = TT.sh.c;
    if (!FLAG(c)) {
      if (toys.optc==1) toys.optflags |= FLAG_s;
      if (FLAG(s) && isatty(0)) toys.optflags |= FLAG_i;
    }
    if (toys.optc>1) {
      toys.optargs++;
      toys.optc--;
    }
    TT.options |= toys.optflags&0xff;
  }

  // Read environment for exports from parent shell. Note, calls run_sh()
  // which blanks argument sections of TT and this, so parse everything
  // we need from shell command line before that.
  subshell_setup();

  if (TT.options&FLAG_i) {
    if (!getvar("PS1")) setvarval("PS1", getpid() ? "\\$ " : "# ");
    // TODO Set up signal handlers and grab control of this tty.
    // ^C SIGINT ^\ SIGQUIT ^Z SIGTSTP SIGTTIN SIGTTOU SIGCHLD
    // setsid(), setpgid(), tcsetpgrp()...
    xsignal(SIGINT, SIG_IGN);
  }

// TODO unify fmemopen() here with sh_run
  if (cc) ff = fmemopen(cc, strlen(cc), "r");
  else if (TT.options&FLAG_s) ff = (TT.options&FLAG_i) ? 0 : stdin;
  else if (!(ff = fpathopen(*toys.optargs))) perror_exit_raw(*toys.optargs);

  // Read and execute lines from file
  if (do_source(cc ? : *toys.optargs, ff))
    error_exit("%ld:unfinished line"+4*!TT.cc->lineno, TT.cc->lineno);
}

// TODO: ./blah.sh one two three: put one two three in scratch.arg

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

// lib/args.c can't +prefix & "+o history" needs space so parse cmdline here
void set_main(void)
{
  char *cc, *ostr[] = {"braceexpand", "noclobber", "xtrace"};
  int ii, jj, kk, oo = 0, dd = 0;

  // Handle options
  for (ii = 0;; ii++) {
    if ((cc = toys.optargs[ii]) && !(dd = stridx("-+", *cc)+1) && oo--) {
      for (jj = 0; jj<ARRAY_LEN(ostr); jj++) if (!strcmp(cc, ostr[jj])) break;
      if (jj != ARRAY_LEN(ostr)) {
        if (dd==1) TT.options |= OPT_B<<kk;
        else TT.options &= ~(OPT_B<<kk);

        continue;
      }
      error_exit("bad -o %s", cc);
    }
    if (oo>0) for (jj = 0; jj<ARRAY_LEN(ostr); jj++)
      printf("%s\t%s\n", ostr[jj], TT.options&(OPT_B<<jj) ? "on" : "off");
    oo = 0;
    if (!cc || !dd) break;
    for (jj = 1; cc[jj]; jj++) {
      if (cc[jj] == 'o') oo++;
      else if (-1 != (kk = stridx("BCx", cc[jj]))) {
        if (*cc == '-') TT.options |= OPT_B<<kk;
        else TT.options &= ~(OPT_B<<kk);
      } else error_exit("bad -%c", toys.optargs[ii][1]);
    }
  }

  // handle positional parameters
  if (cc) {
    struct arg_list *al, **head;
    struct sh_arg *arg = &TT.cc->arg;

    for (al = *(head = &TT.cc->delete); al; al = *(head = &al->next))
      if (al->arg == (void *)arg->v) break;

    // free last set's memory (if any) so it doesn't accumulate in loop
    if (al) for (jj = arg->c+1; jj; jj--) {
      *head = al->next;
      free(al->arg);
      free(al);
    }

    while (toys.optargs[ii])
      arg_add(arg, push_arg(&TT.cc->delete, strdup(toys.optargs[ii++])));
    push_arg(&TT.cc->delete, arg->v);
  }
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
  struct sh_arg old = TT.cc->arg, *volatile arg = &TT.cc->arg;
  char *s;

  // borrow the $* expand infrastructure (avoiding $* from trap handler race).
  arg->c = 0;
  arg->v = toys.argv;
  arg->c = toys.optc+1;
  s = expand_one_arg("\"$*\"", SEMI_IFS, 0);
  arg->c = 0;
  arg->v = old.v;
  arg->c = old.c;

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
  if (*toys.optargs != TT.isexec) free(*toys.optargs);
  TT.isexec = 0;
  toys.exitval = 127;
  environ = old;
}

// Find + and - jobs. Returns index of plus, writes minus to *minus
int find_plus_minus(int *minus)
{
  long long when, then;
  int i, plus;

  if (minus) *minus = 0;
  for (then = i = plus = 0; i<TT.jobs.c; i++) {
    if ((when = ((struct sh_process *)TT.jobs.v[i])->when) > then) {
      then = when;
      if (minus) *minus = plus;
      plus = i;
    }
  }

  return plus;
}

// Return T.jobs index or -1 from identifier
// Note, we don't return "ambiguous job spec", we return the first hit or -1.
// TODO %% %+ %- %?ab
int find_job(char *s)
{
  char *ss;
  long ll = strtol(s, &ss, 10);
  int i, j;

  if (!TT.jobs.c) return -1;
  if (!*s || (!s[1] && strchr("%+-", *s))) {
    int minus, plus = find_plus_minus(&minus);

    return (*s == '-') ? minus : plus;
  }

  // Is this a %1 numeric jobspec?
  if (s != ss && !*ss)
    for (i = 0; i<TT.jobs.c; i++)
      if (((struct sh_process *)TT.jobs.v[i])->job == ll) return i;

  // Match start of command or %?abc
  for (i = 0; i<TT.jobs.c; i++) {
    struct sh_process *pp = (void *)TT.jobs.v[i];

    if (strstart(&s, *pp->arg.v)) return i;
    if (*s != '?' || !s[1]) continue;
    for (j = 0; j<pp->arg.c; j++) if (strstr(pp->arg.v[j], s+1)) return i;
  }

  return -1;
}

// We pass in dash to avoid looping over every job each time
void print_job(int i, char dash)
{
  struct sh_process *pp = (void *)TT.jobs.v[i];
  char *s = "Run";
  int j;

// TODO Terminated (Exited)
  if (pp->exit<0) s = "Stop";
  else if (pp->exit>126) s = "Kill";
  else if (pp->exit>0) s = "Done";
  printf("[%d]%c  %-6s", pp->job, dash, s);
  for (j = 0; j<pp->raw->c; j++) printf(" %s"+!j, pp->raw->v[j]);
  printf("\n");
}

void jobs_main(void)
{
  int i, j, minus, plus = find_plus_minus(&minus);
  char *s;

// TODO -lnprs

  for (i = 0;;i++) {
    if (toys.optc) {
      if (!(s = toys.optargs[i])) break;
      if ((j = find_job(s+('%' == *s))) == -1) {
        perror_msg("%s: no such job", s);

        continue;
      }
    } else if ((j = i) >= TT.jobs.c) break;

    print_job(i, (i == plus) ? '+' : (i == minus) ? '-' : ' ');
  }
}

void shift_main(void)
{
  long long by = 1;

  if (toys.optc) by = atolx(*toys.optargs);
  by += TT.cc->shift;
  if (by<0 || by>=TT.cc->arg.c) toys.exitval++;
  else TT.cc->shift = by;
}

void source_main(void)
{
  char *name = *toys.optargs;
  FILE *ff = fpathopen(name);

  if (!ff) return perror_msg_raw(name);

  // $0 is shell name, not source file name while running this
// TODO add tests: sh -c "source input four five" one two three
  *toys.optargs = *toys.argv;

  do_source(name, ff);
}
