/* sed.c - stream editor. Thing that does s/// and other stuff.
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/sed.html
 *
 * TODO: lines > 2G could wrap signed int length counters. Not just getline()
 * but N and s///

USE_SED(NEWTOY(sed, "(version)e*f*inEr[+Er]", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_LOCALE))

config SED
  bool "sed"
  default y
  help
    usage: sed [-inrE] [-e SCRIPT]...|SCRIPT [-f SCRIPT_FILE]... [FILE...]

    Stream editor. Apply one or more editing SCRIPTs to each line of input
    (from FILE or stdin) producing output (by default to stdout).

    -e	add SCRIPT to list
    -f	add contents of SCRIPT_FILE to list
    -i	Edit each file in place.
    -n	No default output. (Use the p command to output matched lines.)
    -r	Use extended regular expression syntax.
    -E	Alias for -r.
    -s	Treat input files separately (implied by -i)

    A SCRIPT is a series of one or more COMMANDs separated by newlines or
    semicolons. All -e SCRIPTs are concatenated together as if separated
    by newlines, followed by all lines from -f SCRIPT_FILEs, in order.
    If no -e or -f SCRIPTs are specified, the first argument is the SCRIPT.

    Each COMMAND may be preceded by an address which limits the command to
    apply only to the specified line(s). Commands without an address apply to
    every line. Addresses are of the form:

      [ADDRESS[,ADDRESS]]COMMAND

    The ADDRESS may be a decimal line number (starting at 1), a /regular
    expression/ within a pair of forward slashes, or the character "$" which
    matches the last line of input. (In -s or -i mode this matches the last
    line of each file, otherwise just the last line of the last file.) A single
    address matches one line, a pair of comma separated addresses match
    everything from the first address to the second address (inclusive). If
    both addresses are regular expressions, more than one range of lines in
    each file can match.

    REGULAR EXPRESSIONS in sed are started and ended by the same character
    (traditionally / but anything except a backslash or a newline works).
    Backslashes may be used to escape the delimiter if it occurs in the
    regex, and for the usual printf escapes (\abcefnrtv and octal, hex,
    and unicode). An empty regex repeats the previous one. ADDRESS regexes
    (above) require the first delimeter to be escaped with a backslash when
    it isn't a forward slash (to distinguish it from the COMMANDs below).

    Sed mostly operates on individual lines one at a time. It reads each line,
    processes it, and either writes it to the output or discards it before
    reading the next line. Sed can remember one additional line in a separate
    buffer (using the h, H, g, G, and x commands), and can read the next line
    of input early (using the n and N command), but other than that command
    scripts operate on individual lines of text.

    Each COMMAND starts with a single character. The following commands take
    no arguments:

      {  Start a new command block, continuing until a corresponding "}".
         Command blocks may nest. If the block has an address, commands within
         the block are only run for lines within the block's address range.

      }  End command block (this command cannot have an address)

      d  Delete this line and move on to the next one
         (ignores remaining COMMANDs)

      D  Delete one line of input and restart command SCRIPT (same as "d"
         unless you've glued lines together with "N" or similar)

      g  Get remembered line (overwriting current line)

      G  Get remembered line (appending to current line)

      h  Remember this line (overwriting remembered line)

      H  Remember this line (appending to remembered line, if any)

      l  Print line, escaping \abfrtv (but not newline), octal escaping other
         nonprintable characters, wrapping lines to terminal width with a
         backslash, and appending $ to actual end of line.

      n  Print default output and read next line, replacing current line
         (If no next line available, quit processing script)

      N  Append next line of input to this line, separated by a newline
         (This advances the line counter for address matching and "=", if no
         next line available quit processing script without default output)

      p  Print this line

      P  Print this line up to first newline (from "N")

      q  Quit (print default output, no more commands processed or lines read)

      x  Exchange this line with remembered line (overwrite in both directions)

      =  Print the current line number (followed by a newline)

    The following commands (may) take an argument. The "text" arguments (to
    the "a", "b", and "c" commands) may end with an unescaped "\" to append
    the next line (for which leading whitespace is not skipped), and also
    treat ";" as a literal character (use "\;" instead).

      a [text]   Append text to output before attempting to read next line

      b [label]  Branch, jumps to :label (or with no label, to end of SCRIPT)

      c [text]   Delete line, output text at end of matching address range
                 (ignores remaining COMMANDs)

      i [text]   Print text

      r [file]   Append contents of file to output before attempting to read
                 next line.

      s/S/R/F    Search for regex S, replace matched text with R using flags F.
                 The first character after the "s" (anything but newline or
                 backslash) is the delimiter, escape with \ to use normally.

                 The replacement text may contain "&" to substitute the matched
                 text (escape it with backslash for a literal &), or \1 through
                 \9 to substitute a parenthetical subexpression in the regex.
                 You can also use the normal backslash escapes such as \n and
                 a backslash at the end of the line appends the next line.

                 The flags are:

                 [0-9]    A number, substitute only that occurrence of pattern
                 g        Global, substitute all occurrences of pattern
                 i        Ignore case when matching
                 p        Print the line if match was found and replaced
                 w [file] Write (append) line to file if match replaced

      t [label]  Test, jump to :label only if an "s" command found a match in
                 this line since last test (replacing with same text counts)

      T [label]  Test false, jump only if "s" hasn't found a match.

      w [file]   Write (append) line to file

      y/old/new/ Change each character in 'old' to corresponding character
                 in 'new' (with standard backslash escapes, delimiter can be
                 any repeated character except \ or \n)

      : [label]  Labeled target for jump commands

      #  Comment, ignore rest of this line of SCRIPT

    Deviations from posix: allow extended regular expressions with -r,
    editing in place with -i, separate with -s, printf escapes in text, line
    continuations, semicolons after all commands, 2-address anywhere an
    address is allowed, "T" command, multiline continuations for [abc],
    \; to end [abc] argument before end of line.
*/

#define FOR_sed
#include "toys.h"

GLOBALS(
  struct arg_list *f;
  struct arg_list *e;

  // processed pattern list
  struct double_list *pattern;

  char *nextline, *remember;
  void *restart, *lastregex;
  long nextlen, rememberlen, count;
  int fdout, noeol;
  unsigned xx;
)

struct step {
  struct step *next, *prev;

  // Begin and end of each match
  long lmatch[2];
  int rmatch[2], arg1, arg2, w; // offsets because remalloc()
  unsigned not, hit, sflags;
  char c; // action
};

// Write out line with potential embedded NUL, handling eol/noeol
static int emit(char *line, long len, int eol)
{
  int l, old = line[len];

  if (TT.noeol && !writeall(TT.fdout, "\n", 1)) return 1;
  if (eol) line[len++] = '\n';
  if (!len) return 0;
  TT.noeol = len && !eol;
  l = writeall(TT.fdout, line, len);
  if (eol) line[len-1] = old;
  if (l != len) {
    perror_msg("short write");

    return 1;
  }

  return 0;
}

// Do regex matching handling embedded NUL bytes in string. Note that
// neither the pattern nor the match can currently include NUL bytes
// (even with wildcards) and string must be null terminated.
static int ghostwheel(regex_t *preg, char *string, long len, int nmatch,
  regmatch_t pmatch[], int eflags)
{
  char *s = string;

  for (;;) {
    long ll = 0;
    int rc;

    while (len && !*s) {
      s++;
      len--;
    }
    while (s[ll] && ll<len) ll++;

    rc = regexec(preg, s, nmatch, pmatch, eflags);
    if (!rc) {
      for (rc = 0; rc<nmatch && pmatch[rc].rm_so!=-1; rc++) {
        pmatch[rc].rm_so += s-string;
        pmatch[rc].rm_eo += s-string;
      }
          
      return 0;
    }
    if (ll==len) return rc;

    s += ll;
    len -= ll;
  }
}

// Extend allocation to include new string, with newline between if newlen<0

static char *extend_string(char **old, char *new, int oldlen, int newlen)
{
  int newline = newlen < 0;
  char *s;

  if (newline) newlen = -newlen;
  s = *old = xrealloc(*old, oldlen+newlen+newline+1);
  if (newline) s[oldlen++] = '\n';
  memcpy(s+oldlen, new, newlen);
  s[oldlen+newlen] = 0;

  return s+oldlen+newlen+1;
}

// An empty regex repeats the previous one
void *get_regex(void *trump, int offset)
{
  if (!offset) {
    if (!TT.lastregex) error_exit("no previous regex");
    return TT.lastregex;
  }

  return TT.lastregex = offset+(char *)trump;
}

// Apply pattern to line from input file
static void walk_pattern(char **pline, long plen)
{
  struct append {
    struct append *next, *prev;
    int file;
    char *str;
  } *append = 0;
  char *line = TT.nextline;
  long len = TT.nextlen;
  struct step *logrus;
  int eol = 0, tea = 0;

  // Grab next line for deferred processing (EOF detection: we get a NULL
  // pline at EOF to flush last line). Note that only end of _last_ input
  // file matches $ (unless we're doing -i).
  TT.nextline = 0;
  TT.nextlen = 0;
  if (pline) {
    TT.nextline = *pline;
    TT.nextlen = plen;
    *pline = 0;
  }

  if (!line || !len) return;
  if (line[len-1] == '\n') line[--len] = eol++;
  TT.count++;

  // The restart-1 is because we added one to make sure it wasn't NULL,
  // otherwise N as last command would restart script
  logrus = TT.restart ? ((struct step *)TT.restart)-1 : (void *)TT.pattern;
  TT.restart = 0;

  while (logrus) {
    char *str, c = logrus->c;

    // Have we got a line or regex matching range for this rule?
    if (*logrus->lmatch || *logrus->rmatch) {
      int miss = 0;
      long lm;

      // In a match that might end?
      if (logrus->hit) {
        if (!(lm = logrus->lmatch[1])) {
          if (!logrus->rmatch[1]) logrus->hit = 0;
          else {
            void *rm = get_regex(logrus, logrus->rmatch[1]);

            // regex match end includes matching line, so defer deactivation
            if (line && !ghostwheel(rm, line, len, 0, 0, 0)) miss = 1;
          }
        } else if (lm > 0 && lm < TT.count) logrus->hit = 0;

      // Start a new match?
      } else {
        if (!(lm = *logrus->lmatch)) {
          void *rm = get_regex(logrus, *logrus->rmatch);

          if (line && !ghostwheel(rm, line, len, 0, 0, 0)) logrus->hit++;
        } else if (lm == TT.count || (lm == -1 && !pline)) logrus->hit++;

        if (!logrus->lmatch[1] && !logrus->rmatch[1]) miss = 1;
      } 

      // Didn't match?
      lm = !(logrus->hit ^ logrus->not);

      // Deferred disable from regex end match
      if (miss || logrus->lmatch[1] == TT.count) logrus->hit = 0;

      if (lm) {
        // Handle skipping curly bracket command group
        if (c == '{') {
          int curly = 1;

          while (curly) {
            logrus = logrus->next;
            if (logrus->c == '{') curly++;
            if (logrus->c == '}') curly--;
          }
        }
        logrus = logrus->next;
        continue;
      }
    }

    // A deleted line can still update line match state for later commands
    if (!line) {
      logrus = logrus->next;
      continue;
    }

    // Process command

    if (c=='a' || c=='r') {
      struct append *a = xzalloc(sizeof(struct append));
      a->str = logrus->arg1+(char *)logrus;
      a->file = c=='r';
      dlist_add_nomalloc((void *)&append, (void *)a);
    } else if (c=='b' || c=='t' || c=='T') {
      int t = tea;

      if (c != 'b') tea = 0;
      if (c=='b' || t^(c=='T')) {
        if (!logrus->arg1) break;
        str = logrus->arg1+(char *)logrus;
        for (logrus = (void *)TT.pattern; logrus; logrus = logrus->next)
          if (logrus->c == ':' && !strcmp(logrus->arg1+(char *)logrus, str))
            break;
        if (!logrus) error_exit("no :%s", str);
      }
    } else if (c=='c') {
      str = logrus->arg1+(char *)logrus;
      if (!logrus->hit) emit(str, strlen(str), 1);
      free(line);
      line = 0;
      continue;
    } else if (c=='d') {
      free(line);
      line = 0;
      continue;
    } else if (c=='D') {
      // Delete up to \n or end of buffer
      str = line;
      while ((str-line)<len) if (*(str++) == '\n') break;
      len -= str - line;
      memmove(line, str, len);

      // if "delete" blanks line, disable further processing
      // otherwise trim and restart script
      if (!len) {
        free(line);
        line = 0;
      } else {
        line[len] = 0;
        logrus = (void *)TT.pattern;
      }
      continue;
    } else if (c=='g') {
      free(line);
      line = xstrdup(TT.remember);
      len = TT.rememberlen;
    } else if (c=='G') {
      line = xrealloc(line, len+TT.rememberlen+2);
      line[len++] = '\n';
      memcpy(line+len, TT.remember, TT.rememberlen);
      line[len += TT.rememberlen] = 0;
    } else if (c=='h') {
      free(TT.remember);
      TT.remember = xstrdup(line);
      TT.rememberlen = len;
    } else if (c=='H') {
      TT.remember = xrealloc(TT.remember, TT.rememberlen+len+2);
      TT.remember[TT.rememberlen++] = '\n';
      memcpy(TT.remember+TT.rememberlen, line, len);
      TT.remember[TT.rememberlen += len] = 0;
    } else if (c=='i') {
      str = logrus->arg1+(char *)logrus;
      emit(str, strlen(str), 1);
    } else if (c=='l') {
      int i, x, off;

      if (!TT.xx) {
        terminal_size(&TT.xx, 0);
        if (!TT.xx) TT.xx = 80;
        if (TT.xx > sizeof(toybuf)-10) TT.xx = sizeof(toybuf)-10;
        if (TT.xx > 4) TT.xx -= 4;
      }

      for (i = off = 0; i<len; i++) {
        if (off >= TT.xx) {
          toybuf[off++] = '\\';
          emit(toybuf, off, 1);
          off = 0;
        }
        x = stridx("\\\a\b\f\r\t\v", line[i]);
        if (x != -1) {
          toybuf[off++] = '\\';
          toybuf[off++] = "\\abfrtv"[x];
        } else if (line[i] >= ' ') toybuf[off++] = line[i];
        else off += sprintf(toybuf+off, "\\%03o", line[i]);
      }
      toybuf[off++] = '$';
      emit(toybuf, off, 1);
    } else if (c=='n') {
      TT.restart = logrus->next+1;

      break;
    } else if (c=='N') {
      // Can't just grab next line because we could have multiple N and
      // we need to actually read ahead to get N;$p EOF detection right.
      if (pline) {
        TT.restart = logrus->next+1;
        extend_string(&line, TT.nextline, len, -TT.nextlen);
        free(TT.nextline);
        TT.nextline = line;
        TT.nextlen += len + 1;
        line = 0;
      }

      // Pending append goes out right after N
      goto done; 
    } else if (c=='p' || c=='P') {
      char *l = (c=='P') ? strchr(line, '\n') : 0;

      if (emit(line, l ? l-line : len, eol)) break;
    } else if (c=='q') {
      if (pline) *pline = (void *)1;
      free(TT.nextline);
      TT.nextline = 0;
      TT.nextlen = 0;

      break;
    } else if (c=='s') {
      char *rline = line, *new = logrus->arg2 + (char *)logrus, *swap, *rswap;
      regmatch_t *match = (void *)toybuf;
      regex_t *reg = get_regex(logrus, logrus->arg1);
      int mflags = 0, count = 0, zmatch = 1, rlen = len, mlen, off, newlen;

      // Find match in remaining line (up to remaining len)
      while (!ghostwheel(reg, rline, rlen, 10, match, mflags)) {
        mflags = REG_NOTBOL;

        // Zero length matches don't count immediately after a previous match
        mlen = match[0].rm_eo-match[0].rm_so;
        if (!mlen && !zmatch) {
          if (!rlen--) break;
          rline++;
          zmatch++;
          continue;
        } else zmatch = 0;

        // If we're replacing only a specific match, skip if this isn't it
        off = logrus->sflags>>3;
        if (off && off != ++count) {
          rline += match[0].rm_eo;
          rlen -= match[0].rm_eo;

          continue;
        }
        // The fact getline() can allocate unbounded amounts of memory is
        // a bigger issue, but while we're here check for integer overflow
        if (match[0].rm_eo > INT_MAX) perror_exit(0);

        // newlen = strlen(new) but with \1 and & and printf escapes
        for (off = newlen = 0; new[off]; off++) {
          int cc = -1;

          if (new[off] == '&') cc = 0;
          else if (new[off] == '\\') cc = new[++off] - '0';
          if (cc < 0 || cc > 9) {
            newlen++;
            continue;
          }
          newlen += match[cc].rm_eo-match[cc].rm_so;
        }

        // Allocate new size, copy start/end around match. (Can't extend in
        // place because backrefs may refer to text after it's overwritten.)
        len += newlen-mlen;
        swap = xmalloc(len+1);
        rswap = swap+(rline-line)+match[0].rm_so;
        memcpy(swap, line, (rline-line)+match[0].rm_so);
        memcpy(rswap+newlen, rline+match[0].rm_eo, (rlen -= match[0].rm_eo)+1);

        // copy in new replacement text
        for (off = mlen = 0; new[off]; off++) {
          int cc = 0, ll;

          if (new[off] == '\\') {
            cc = new[++off] - '0';
            if (cc<0 || cc>9) {
              if (!(rswap[mlen++] = unescape(new[off])))
                rswap[mlen-1] = new[off];

              continue;
            } else if (match[cc].rm_so == -1) error_exit("no s//\\%d/", cc);
          } else if (new[off] != '&') {
            rswap[mlen++] = new[off];

            continue;
          }

          ll = match[cc].rm_eo-match[cc].rm_so;
          memcpy(rswap+mlen, rline+match[cc].rm_so, ll);
          mlen += ll;
        }

        rline = rswap+newlen;
        free(line);
        line = swap;

        // Stop after first substitution unless we have flag g
        if (!(logrus->sflags & 2)) break;
      }

      if (mflags) {
        // flag p
        if (logrus->sflags & 4) emit(line, len, eol);

        tea = 1;
        if (logrus->w) goto writenow;
      }
    } else if (c=='w') {
      int fd, noeol;
      char *name;

writenow:
      // Swap out emit() context
      fd = TT.fdout;
      noeol = TT.noeol;

      // We save filehandle and newline status before filename
      name = logrus->w + (char *)logrus;
      memcpy(&TT.fdout, name, 4);
      name += 4;
      TT.noeol = *(name++);

      // write, then save/restore context
      if (emit(line, len, eol))
        perror_exit("w '%s'", logrus->arg1+(char *)logrus);
      *(--name) = TT.noeol;
      TT.noeol = noeol;
      TT.fdout = fd;
    } else if (c=='x') {
      long swap = TT.rememberlen;

      str = TT.remember;
      TT.remember = line;
      line = str;
      TT.rememberlen = len;
      len = swap;
    } else if (c=='y') {
      char *from, *to = (char *)logrus;
      int i, j;

      from = to+logrus->arg1;
      to += logrus->arg2;

      for (i = 0; i < len; i++) {
        j = stridx(from, line[i]);
        if (j != -1) line[i] = to[j];
      }
    } else if (c=='=') {
      sprintf(toybuf, "%ld", TT.count);
      emit(toybuf, strlen(toybuf), 1);
    }

    logrus = logrus->next;
  }

  if (line && !(toys.optflags & FLAG_n)) emit(line, len, eol);

done:
  free(line);

  if (dlist_terminate(append)) while (append) {
    struct append *a = append->next;

    if (append->file) {
      int fd = open(append->str, O_RDONLY);

      // Force newline if noeol pending
      if (fd != -1) {
        if (TT.noeol) xwrite(TT.fdout, "\n", 1);
        TT.noeol = 0;
        xsendfile(fd, TT.fdout);
        close(fd);
      }
    } else emit(append->str, strlen(append->str), 1);
    free(append);
    append = a;
  }
}

// Genericish function, can probably get moved to lib.c

// Iterate over lines in file, calling function. Function can write 0 to
// the line pointer if they want to keep it, or 1 to terminate processing,
// otherwise line is freed. Passed file descriptor is closed at the end.
static void do_lines(int fd, char *name, void (*call)(char **pline, long len))
{
  FILE *fp = fd ? xfdopen(fd, "r") : stdin;

  for (;;) {
    char *line = 0;
    ssize_t len;

    len = getline(&line, (void *)&len, fp);
    if (len > 0) {
      call(&line, len);
      if (line == (void *)1) break;
      free(line);
    } else break;
  }

  if (fd) fclose(fp);
}

static void do_sed(int fd, char *name)
{
  int i = toys.optflags & FLAG_i;
  char *tmp;

  if (i) {
    struct step *primal;

    if (!fd && *name=='-') {
      error_msg("-i on stdin");
      return;
    }
    TT.fdout = copy_tempfile(fd, name, &tmp);
    TT.count = 0;
    for (primal = (void *)TT.pattern; primal; primal = primal->next)
      primal->hit = 0;
  }
  do_lines(fd, name, walk_pattern);
  if (i) {
    walk_pattern(0, 0);
    replace_tempfile(-1, TT.fdout, &tmp);
    TT.fdout = 1;
    TT.nextline = 0;
    TT.nextlen = TT.noeol = 0;
  }
}

// Copy chunk of string between two delimiters, converting printf escapes.
// returns processed copy of string (0 if error), *pstr advances to next
// unused char. if delim (or *delim) is 0 uses/saves starting char as delimiter
// if regxex, ignore delimiter in [ranges]
static char *unescape_delimited_string(char **pstr, char *delim, int regex)
{
  char *to, *from, mode = 0, d;

  to = from = *pstr;
  if (!delim || !*delim) {
    if (!(d = *(from++))) return 0;
    if (d == '\\') d = *(from++);
    if (!d || d == '\\') return 0;
    if (delim) *delim = d;
  } else d = *delim;
  to = delim = xmalloc(strlen(*pstr)+1);

  while (mode || *from != d) {
    if (!*from) return 0;

    // delimiter in regex character range doesn't count
    if (*from == '[') {
      mode = '[';
      if (from[1] == ']') *(to++) = *(from++);
    } else if (mode && *from == ']') mode = 0;
    else if (*from == '\\') {
      if (!from[1]) return 0;

      // Check escaped end delimiter before printf style escapes.
      if (from[1] == d) from++;
      else if (from[1]=='\\') *(to++) = *(from++);
      else {
        char c = unescape(from[1]);

        if (c) {
          *(to++) = c;
          from+=2;
          continue;
        } else *(to++) = *(from++);
      }
    }
    *(to++) = *(from++);
  }
  *to = 0;
  *pstr = from+1;

  return delim;
}

// Translate primal pattern into walkable form.
static void jewel_of_judgement(char **pline, long len)
{
  struct step *corwin = (void *)TT.pattern;
  char *line, *reg, c, *errstart;
  int i;

  line = errstart = pline ? *pline : "";
  if (len && line[len-1]=='\n') line[--len] = 0;

  // Append additional line to pattern argument string?
  // We temporarily repurpose "hit" to indicate line continuations
  if (corwin && corwin->prev->hit) {
    if (!*pline) error_exit("unfinished %c", corwin->prev->c);;
    // Remove half-finished entry from list so remalloc() doesn't confuse it
    TT.pattern = TT.pattern->prev;
    corwin = dlist_pop(&TT.pattern);
    c = corwin->c;
    reg = (char *)corwin;
    reg += corwin->arg1 + strlen(reg + corwin->arg1);

    // Resume parsing for 'a' or 's' command
    if (corwin->hit < 256) goto resume_s;
    else goto resume_a;
  }

  // Loop through commands in line

  corwin = 0;
  for (;;) {
    if (corwin) dlist_add_nomalloc(&TT.pattern, (void *)corwin);

    for (;;) {
      while (isspace(*line) || *line == ';') line++;
      if (*line == '#') while (*line && *line != '\n') line++;
      else break;
    }
    if (!*line) return;

    errstart = line;
    memset(toybuf, 0, sizeof(struct step));
    corwin = (void *)toybuf;
    reg = toybuf + sizeof(struct step);

    // Parse address range (if any)
    for (i = 0; i < 2; i++) {
      if (*line == ',') line++;
      else if (i) break;

      if (isdigit(*line)) corwin->lmatch[i] = strtol(line, &line, 0);
      else if (*line == '$') {
        corwin->lmatch[i] = -1;
        line++;
      } else if (*line == '/' || *line == '\\') {
        char *s = line;

        if (!(s = unescape_delimited_string(&line, 0, 1))) goto brand;
        if (!*s) corwin->rmatch[i] = 0;
        else {
          xregcomp((void *)reg, s, (toys.optflags & FLAG_r)*REG_EXTENDED);
          corwin->rmatch[i] = reg-toybuf;
          reg += sizeof(regex_t);
        }
        free(s);
      } else break;
    }

    while (isspace(*line)) line++;
    if (!*line) break;

    while (*line == '!') {
      corwin->not = 1;
      line++;
    }
    while (isspace(*line)) line++;

    c = corwin->c = *(line++);
    if (strchr("}:", c) && i) break;
    if (strchr("aiqr=", c) && i>1) break;

    // Add step to pattern
    corwin = xmalloc(reg-toybuf);
    memcpy(corwin, toybuf, reg-toybuf);
    reg = (reg-toybuf) + (char *)corwin;

    // Parse arguments by command type
    if (c == '{') TT.nextlen++;
    else if (c == '}') {
      if (!TT.nextlen--) break;
    } else if (c == 's') {
      char *fiona, delim = 0;

      // s/pattern/replacement/flags

      // line continuations use arg1, so we fill out arg2 first (since the
      // regex part can't be multiple lines) and swap them back later.

      // get pattern (just record, we parse it later)
      corwin->arg2 = reg - (char *)corwin;
      if (!(TT.remember = unescape_delimited_string(&line, &delim, 1)))
        goto brand;

      reg += sizeof(regex_t);
      corwin->arg1 = reg-(char *)corwin;
      corwin->hit = delim;
resume_s:
      // get replacement - don't replace escapes because \1 and \& need
      // processing later, after we replace \\ with \ we can't tell \\1 from \1
      fiona = line;
      while (*fiona != corwin->hit) {
        if (!*fiona) goto brand;
        if (*fiona++ == '\\') {
          if (!*fiona || *fiona == '\n') {
            fiona[-1] = '\n';
            break;
          }
          fiona++;
        }
      }

      reg = extend_string((void *)&corwin, line, reg-(char *)corwin,fiona-line);
      line = fiona;
      // line continuation? (note: '\n' can't be a valid delim).
      if (*line == corwin->hit) corwin->hit = 0;
      else {
        if (!*line) continue;
        reg--;
        line++;
        goto resume_s;
      }

      // swap arg1/arg2 so they're back in order arguments occur.
      i = corwin->arg1;
      corwin->arg1 = corwin->arg2;
      corwin->arg2 = i;

      // get flags
      for (line++; *line; line++) {
        long l;

        if (isspace(*line) && *line != '\n') continue;

        if (0 <= (l = stridx("igp", *line))) corwin->sflags |= 1<<l;
        else if (!(corwin->sflags>>3) && 0<(l = strtol(line, &line, 10))) {
          corwin->sflags |= l << 3;
          line--;
        } else break;
      }

      // We deferred actually parsing the regex until we had the s///i flag
      // allocating the space was done by extend_string() above
      if (!*TT.remember) corwin->arg1 = 0;
      else xregcomp((void *)(corwin->arg1 + (char *)corwin), TT.remember,
        ((toys.optflags & FLAG_r)*REG_EXTENDED)|((corwin->sflags&1)*REG_ICASE));
      free(TT.remember);
      TT.remember = 0;
      if (*line == 'w') {
        line++;
        goto writenow;
      }
    } else if (c == 'w') {
      int fd, delim;
      char *cc;

      // Since s/// uses arg1 and arg2, and w needs a persistent filehandle and
      // eol status, and to retain the filename for error messages, we'd need
      // to go up to arg5 just for this. Compromise: dynamically allocate the
      // filehandle and eol status.

writenow:
      while (isspace(*line)) line++;
      if (!*line) goto brand;
      for (cc = line; *cc; cc++) if (*cc == '\\' && cc[1] == ';') break;
      delim = *cc;
      *cc = 0;
      fd = xcreate(line, O_WRONLY|O_CREAT|O_TRUNC, 0644);
      *cc = delim;

      corwin->w = reg - (char *)corwin;
      corwin = xrealloc(corwin, corwin->w+(cc-line)+6);
      reg = corwin->w + (char *)corwin;

      memcpy(reg, &fd, 4);
      reg += 4;
      *(reg++) = 0;
      memcpy(reg, line, delim);
      reg += delim;
      *(reg++) = 0;

      line = cc;
      if (delim) line += 2;
    } else if (c == 'y') {
      char *s, delim = 0;
      int len;

      if (!(s = unescape_delimited_string(&line, &delim, 0))) goto brand;
      corwin->arg1 = reg-(char *)corwin;
      len = strlen(s);
      reg = extend_string((void *)&corwin, s, reg-(char *)corwin, len);
      free(s);
      corwin->arg2 = reg-(char *)corwin;
      if (!(s = unescape_delimited_string(&line, &delim, 0))) goto brand;
      if (len != strlen(s)) goto brand;
      reg = extend_string((void *)&corwin, s, reg-(char*)corwin, len);
      free(s);
    } else if (strchr("abcirtTw:", c)) {
      int end;

      while (isspace(*line) && *line != '\n') line++;

      // Resume logic differs from 's' case because we don't add a newline
      // unless it's after something, so we add it on return instead.
resume_a:
      corwin->hit = 0;

      // Trim whitespace from "b ;" and ": blah " but only first space in "w x "
      if (!(end = strcspn(line, strchr("btT:", c) ? "; \t\r\n\v\f" : "\n"))) {
        if (strchr("btT", c)) continue;
        else if (!corwin->arg1) break;
      }

      // Extend allocation to include new string. We use offsets instead of
      // pointers so realloc() moving stuff doesn't break things. Ok to write
      // \n over NUL terminator because call to extend_string() adds it back.
      if (!corwin->arg1) corwin->arg1 = reg - (char*)corwin;
      else if ((corwin+1) != (void *)reg) *(reg++) = '\n';
      reg = extend_string((void *)&corwin, line, reg - (char *)corwin, end);

      // Recopy data to remove escape sequences and handle line continuation.
      if (strchr("aci", c)) {
        reg -= end+1;
        for (i = end; i; i--) {
          if ((*reg++ = *line++)=='\\') {

            // escape at end of line: resume if -e escaped literal newline,
            // else request callback and resume with next line
            if (!--i) {
              *--reg = 0;
              if (*line) {
                line++;
                goto resume_a;
              }
              corwin->hit = 256;
              break;
            }
            if (!(reg[-1] = unescape(*line))) reg[-1] = *line;
            line++;
          }
        }
        *reg = 0;
      } else line += end;

    // Commands that take no arguments
    } else if (!strchr("{dDgGhHlnNpPqx=", c)) break;
  }

brand:
  // Reminisce about chestnut trees.
  error_exit("bad pattern '%s'@%ld (%c)", errstart, line-errstart+1L, *line);
}

void sed_main(void)
{
  struct arg_list *dworkin;
  char **args = toys.optargs;

  // Lie to autoconf when it asks stupid questions, so configure regexes
  // that look for "GNU sed version %f" greater than some old buggy number
  // don't fail us for not matching their narrow expectations.
  if (toys.optflags & FLAG_version) {
    xprintf("This is not GNU sed version 9.0\n");
    return;
  }

  // Need a pattern. If no unicorns about, fight serpent and take its eye.
  if (!TT.e && !TT.f) {
    if (!*toys.optargs) error_exit("no pattern");
    (TT.e = xzalloc(sizeof(struct arg_list)))->arg = *(args++);
  }

  // Option parsing infrastructure can't interlace "-e blah -f blah -e blah"
  // so handle all -e, then all -f. (At least the behavior's consistent.)

  for (dworkin = TT.e; dworkin; dworkin = dworkin->next)
    jewel_of_judgement(&dworkin->arg, strlen(dworkin->arg));
  for (dworkin = TT.f; dworkin; dworkin = dworkin->next)
    do_lines(xopen(dworkin->arg, O_RDONLY), dworkin->arg, jewel_of_judgement);
  jewel_of_judgement(0, 0);
  dlist_terminate(TT.pattern);
  if (TT.nextlen) error_exit("no }");  

  TT.fdout = 1;
  TT.remember = xstrdup("");

  // Inflict pattern upon input files
  loopfiles_rw(args, O_RDONLY, 0, 0, do_sed);

  if (!(toys.optflags & FLAG_i)) walk_pattern(0, 0);

  // todo: need to close fd when done for TOYBOX_FREE?
}
