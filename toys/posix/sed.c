/* sed.c - stream editor. Thing that does s/// and other stuff.
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/sed.html
 *
 * TODO: lines > 2G could wrap signed int length counters. Not just getline()
 * but N and s///
 * TODO: make y// handle unicode
 * TODO: handle error return from emit(), error_msg/exit consistently
 *       What's the right thing to do for -i when write fails? Skip to next?

USE_SED(NEWTOY(sed, "(help)(version)e*f*inEr[+Er]", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_LOCALE|TOYFLAG_NOHELP))

config SED
  bool "sed"
  default y
  help
    usage: sed [-inrE] [-e SCRIPT]...|SCRIPT [-f SCRIPT_FILE]... [FILE...]

    Stream editor. Apply one or more editing SCRIPTs to each line of input
    (from FILE or stdin) producing output (by default to stdout).

    -e	add SCRIPT to list
    -f	add contents of SCRIPT_FILE to list
    -i	Edit each file in place
    -n	No default output (use the p command to output matched lines)
    -r	Use extended regular expression syntax
    -E	Alias for -r
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

// Linked list of parsed sed commands. Offset fields indicate location where
// regex or string starts, ala offset+(char *)struct, because we remalloc()
// these to expand them for multiline inputs, and pointers would have to be
// individually adjusted.

struct sedcmd {
  struct sedcmd *next, *prev;

  // Begin and end of each match
  long lmatch[2]; // line number of match
  int rmatch[2];  // offset of regex struct for prefix matches (/abc/,/def/p)
  int arg1, arg2, w; // offset of two arguments per command, plus s//w filename
  unsigned not, hit;
  unsigned sflags; // s///flag bits: i=1, g=2, p=4
  char c; // action
};

// Write out line with potential embedded NUL, handling eol/noeol
static int emit(char *line, long len, int eol)
{
  int l, old = line[len];

  if (TT.noeol && !writeall(TT.fdout, "\n", 1)) return 1;
  TT.noeol = !eol;
  if (eol) line[len++] = '\n';
  if (!len) return 0;
  l = writeall(TT.fdout, line, len);
  if (eol) line[len-1] = old;
  if (l != len) {
    perror_msg("short write");

    return 1;
  }

  return 0;
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
static void *get_regex(void *trump, int offset)
{
  if (!offset) {
    if (!TT.lastregex) error_exit("no previous regex");
    return TT.lastregex;
  }

  return TT.lastregex = offset+(char *)trump;
}

// Apply pattern to line from input file
static void process_line(char **pline, long plen)
{
  struct append {
    struct append *next, *prev;
    int file;
    char *str;
  } *append = 0;
  char *line = TT.nextline;
  long len = TT.nextlen;
  struct sedcmd *command;
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
  command = TT.restart ? ((struct sedcmd *)TT.restart)-1 : (void *)TT.pattern;
  TT.restart = 0;

  while (command) {
    char *str, c = command->c;

    // Have we got a line or regex matching range for this rule?
    if (*command->lmatch || *command->rmatch) {
      int miss = 0;
      long lm;

      // In a match that might end?
      if (command->hit) {
        if (!(lm = command->lmatch[1])) {
          if (!command->rmatch[1]) command->hit = 0;
          else {
            void *rm = get_regex(command, command->rmatch[1]);

            // regex match end includes matching line, so defer deactivation
            if (line && !regexec0(rm, line, len, 0, 0, 0)) miss = 1;
          }
        } else if (lm > 0 && lm < TT.count) command->hit = 0;

      // Start a new match?
      } else {
        if (!(lm = *command->lmatch)) {
          void *rm = get_regex(command, *command->rmatch);

          if (line && !regexec0(rm, line, len, 0, 0, 0)) command->hit++;
        } else if (lm == TT.count || (lm == -1 && !pline)) command->hit++;

        if (!command->lmatch[1] && !command->rmatch[1]) miss = 1;
      } 

      // Didn't match?
      lm = !(command->hit ^ command->not);

      // Deferred disable from regex end match
      if (miss || command->lmatch[1] == TT.count) command->hit = 0;

      if (lm) {
        // Handle skipping curly bracket command group
        if (c == '{') {
          int curly = 1;

          while (curly) {
            command = command->next;
            if (command->c == '{') curly++;
            if (command->c == '}') curly--;
          }
        }
        command = command->next;
        continue;
      }
    }

    // A deleted line can still update line match state for later commands
    if (!line) {
      command = command->next;
      continue;
    }

    // Process command

    if (c=='a' || c=='r') {
      struct append *a = xzalloc(sizeof(struct append));
      if (command->arg1) a->str = command->arg1+(char *)command;
      a->file = c=='r';
      dlist_add_nomalloc((void *)&append, (void *)a);
    } else if (c=='b' || c=='t' || c=='T') {
      int t = tea;

      if (c != 'b') tea = 0;
      if (c=='b' || t^(c=='T')) {
        if (!command->arg1) break;
        str = command->arg1+(char *)command;
        for (command = (void *)TT.pattern; command; command = command->next)
          if (command->c == ':' && !strcmp(command->arg1+(char *)command, str))
            break;
        if (!command) error_exit("no :%s", str);
      }
    } else if (c=='c') {
      str = command->arg1+(char *)command;
      if (!command->hit) emit(str, strlen(str), 1);
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
        command = (void *)TT.pattern;
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
      str = command->arg1+(char *)command;
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
      TT.restart = command->next+1;

      break;
    } else if (c=='N') {
      // Can't just grab next line because we could have multiple N and
      // we need to actually read ahead to get N;$p EOF detection right.
      if (pline) {
        TT.restart = command->next+1;
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
      char *rline = line, *new = command->arg2 + (char *)command, *swap, *rswap;
      regmatch_t *match = (void *)toybuf;
      regex_t *reg = get_regex(command, command->arg1);
      int mflags = 0, count = 0, zmatch = 1, rlen = len, mlen, off, newlen;

      // Find match in remaining line (up to remaining len)
      while (!regexec0(reg, rline, rlen, 10, match, mflags)) {
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
        off = command->sflags>>3;
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
        if (!(command->sflags & 2)) break;
      }

      if (mflags) {
        // flag p
        if (command->sflags & 4) emit(line, len, eol);

        tea = 1;
        if (command->w) goto writenow;
      }
    } else if (c=='w') {
      int fd, noeol;
      char *name;

writenow:
      // Swap out emit() context
      fd = TT.fdout;
      noeol = TT.noeol;

      // We save filehandle and newline status before filename
      name = command->w + (char *)command;
      memcpy(&TT.fdout, name, 4);
      name += 4;
      TT.noeol = *(name++);

      // write, then save/restore context
      if (emit(line, len, eol))
        perror_exit("w '%s'", command->arg1+(char *)command);
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
      char *from, *to = (char *)command;
      int i, j;

      from = to+command->arg1;
      to += command->arg2;

      for (i = 0; i < len; i++) {
        j = stridx(from, line[i]);
        if (j != -1) line[i] = to[j];
      }
    } else if (c=='=') {
      sprintf(toybuf, "%ld", TT.count);
      emit(toybuf, strlen(toybuf), 1);
    }

    command = command->next;
  }

  if (line && !(toys.optflags & FLAG_n)) emit(line, len, eol);

done:
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
    } else if (append->str) emit(append->str, strlen(append->str), 1);
    else emit(line, 0, 0);
    free(append);
    append = a;
  }
  free(line);
}

// Callback called on each input file
static void do_sed(int fd, char *name)
{
  int i = toys.optflags & FLAG_i;
  char *tmp;

  if (i) {
    struct sedcmd *command;

    if (!fd) {
      error_msg("-i on stdin");
      return;
    }
    TT.fdout = copy_tempfile(fd, name, &tmp);
    TT.count = 0;
    for (command = (void *)TT.pattern; command; command = command->next)
      command->hit = 0;
  }
  do_lines(fd, process_line);
  if (i) {
    process_line(0, 0);
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
static char *unescape_delimited_string(char **pstr, char *delim)
{
  char *to, *from, mode = 0, d;

  // Grab leading delimiter (if necessary), allocate space for new string
  from = *pstr;
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
      if (!mode) {
        mode = ']';
        if (from[1]=='-' || from[1]==']') *(to++) = *(from++);
      } else if (mode == ']' && strchr(".=:", from[1])) {
        *(to++) = *(from++);
        mode = *from;
      }
    } else if (*from == mode) {
      if (mode == ']') mode = 0;
      else {
        *(to++) = *(from++);
        mode = ']';
      }
    // Length 1 range (X-X with same X) is "undefined" and makes regcomp err,
    // but the perl build does it, so we need to filter it out.
    } else if (mode && *from == '-' && from[-1] == from[1]) {
      from+=2;
      continue;
    } else if (*from == '\\') {
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
        } else if (!mode) *(to++) = *(from++);
      }
    }
    *(to++) = *(from++);
  }
  *to = 0;
  *pstr = from+1;

  return delim;
}

// Translate pattern strings into command structures. Each command structure
// is a single allocation (which requires some math and remalloc at times).
static void parse_pattern(char **pline, long len)
{
  struct sedcmd *command = (void *)TT.pattern;
  char *line, *reg, c, *errstart;
  int i;

  line = errstart = pline ? *pline : "";
  if (len && line[len-1]=='\n') line[--len] = 0;

  // Append this line to previous multiline command? (hit indicates type.)
  // During parsing "hit" stores data about line continuations, but in
  // process_line() it means the match range attached to this command
  // is active, so processing the continuation must zero it again.
  if (command && command->prev->hit) {
    // Remove half-finished entry from list so remalloc() doesn't confuse it
    TT.pattern = TT.pattern->prev;
    command = dlist_pop(&TT.pattern);
    c = command->c;
    reg = (char *)command;
    reg += command->arg1 + strlen(reg + command->arg1);

    // Resume parsing for 'a' or 's' command. (Only two that can do this.)
    // TODO: using 256 to indicate 'a' means our s/// delimiter can't be
    // a unicode character.
    if (command->hit < 256) goto resume_s;
    else goto resume_a;
  }

  // Loop through commands in this line.

  command = 0;
  for (;;) {
    if (command) dlist_add_nomalloc(&TT.pattern, (void *)command);

    // If there's no more data on this line, return.
    for (;;) {
      while (isspace(*line) || *line == ';') line++;
      if (*line == '#') while (*line && *line != '\n') line++;
      else break;
    }
    if (!*line) return;

    // We start by writing data into toybuf. Later we'll allocate the
    // ex

    errstart = line;
    memset(toybuf, 0, sizeof(struct sedcmd));
    command = (void *)toybuf;
    reg = toybuf + sizeof(struct sedcmd);

    // Parse address range (if any)
    for (i = 0; i < 2; i++) {
      if (*line == ',') line++;
      else if (i) break;

      if (isdigit(*line)) command->lmatch[i] = strtol(line, &line, 0);
      else if (*line == '$') {
        command->lmatch[i] = -1;
        line++;
      } else if (*line == '/' || *line == '\\') {
        char *s = line;

        if (!(s = unescape_delimited_string(&line, 0))) goto error;
        if (!*s) command->rmatch[i] = 0;
        else {
          xregcomp((void *)reg, s, (toys.optflags & FLAG_r)*REG_EXTENDED);
          command->rmatch[i] = reg-toybuf;
          reg += sizeof(regex_t);
        }
        free(s);
      } else break;
    }

    while (isspace(*line)) line++;
    if (!*line) break;

    while (*line == '!') {
      command->not = 1;
      line++;
    }
    while (isspace(*line)) line++;

    c = command->c = *(line++);
    if (strchr("}:", c) && i) break;
    if (strchr("aiqr=", c) && i>1) break;

    // Add step to pattern
    command = xmemdup(toybuf, reg-toybuf);
    reg = (reg-toybuf) + (char *)command;

    // Parse arguments by command type
    if (c == '{') TT.nextlen++;
    else if (c == '}') {
      if (!TT.nextlen--) break;
    } else if (c == 's') {
      char *end, delim = 0;

      // s/pattern/replacement/flags

      // line continuations use arg1 (back at the start of the function),
      // so let's fill out arg2 first (since the regex part can't be multiple
      // lines) and swap them back later.

      // get pattern (just record, we parse it later)
      command->arg2 = reg - (char *)command;
      if (!(TT.remember = unescape_delimited_string(&line, &delim)))
        goto error;

      reg += sizeof(regex_t);
      command->arg1 = reg-(char *)command;
      command->hit = delim;
resume_s:
      // get replacement - don't replace escapes yet because \1 and \& need
      // processing later, after we replace \\ with \ we can't tell \\1 from \1
      end = line;
      while (*end != command->hit) {
        if (!*end) goto error;
        if (*end++ == '\\') {
          if (!*end || *end == '\n') {
            end[-1] = '\n';
            break;
          }
          end++;
        }
      }

      reg = extend_string((void *)&command, line, reg-(char *)command,end-line);
      line = end;
      // line continuation? (note: '\n' can't be a valid delim).
      if (*line == command->hit) command->hit = 0;
      else {
        if (!*line) continue;
        reg--;
        line++;
        goto resume_s;
      }

      // swap arg1/arg2 so they're back in order arguments occur.
      i = command->arg1;
      command->arg1 = command->arg2;
      command->arg2 = i;

      // get flags
      for (line++; *line; line++) {
        long l;

        if (isspace(*line) && *line != '\n') continue;

        if (0 <= (l = stridx("igp", *line))) command->sflags |= 1<<l;
        else if (!(command->sflags>>3) && 0<(l = strtol(line, &line, 10))) {
          command->sflags |= l << 3;
          line--;
        } else break;
      }

      // We deferred actually parsing the regex until we had the s///i flag
      // allocating the space was done by extend_string() above
      if (!*TT.remember) command->arg1 = 0;
      else xregcomp((void *)(command->arg1 + (char *)command), TT.remember,
        ((toys.optflags & FLAG_r)*REG_EXTENDED)|((command->sflags&1)*REG_ICASE));
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
      if (!*line) goto error;
      for (cc = line; *cc; cc++) if (*cc == '\\' && cc[1] == ';') break;
      delim = *cc;
      *cc = 0;
      fd = xcreate(line, O_WRONLY|O_CREAT|O_TRUNC, 0644);
      *cc = delim;

      command->w = reg - (char *)command;
      command = xrealloc(command, command->w+(cc-line)+6);
      reg = command->w + (char *)command;

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

      if (!(s = unescape_delimited_string(&line, &delim))) goto error;
      command->arg1 = reg-(char *)command;
      len = strlen(s);
      reg = extend_string((void *)&command, s, reg-(char *)command, len);
      free(s);
      command->arg2 = reg-(char *)command;
      if (!(s = unescape_delimited_string(&line, &delim))) goto error;
      if (len != strlen(s)) goto error;
      reg = extend_string((void *)&command, s, reg-(char*)command, len);
      free(s);
    } else if (strchr("abcirtTw:", c)) {
      int end;

      // trim leading spaces
      while (isspace(*line) && *line != '\n') line++;

      // Resume logic differs from 's' case because we don't add a newline
      // unless it's after something, so we add it on return instead.
resume_a:
      command->hit = 0;

      // btT: end with space or semicolon, aicrw continue to newline.
      if (!(end = strcspn(line, strchr(":btT", c) ? "; \t\r\n\v\f" : "\n"))) {
        // Argument's optional for btT
        if (strchr("btT", c)) continue;
        else if (!command->arg1) break;
      }

      // Extend allocation to include new string. We use offsets instead of
      // pointers so realloc() moving stuff doesn't break things. Ok to write
      // \n over NUL terminator because call to extend_string() adds it back.
      if (!command->arg1) command->arg1 = reg - (char*)command;
      else if (*(command->arg1+(char *)command)) *(reg++) = '\n';
      else if (!pline) {
        command->arg1 = 0;
        continue;
      }
      reg = extend_string((void *)&command, line, reg - (char *)command, end);

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
              command->hit = 256;
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

error:
  error_exit("bad pattern '%s'@%ld (%c)", errstart, line-errstart+1L, *line);
}

void sed_main(void)
{
  struct arg_list *al;
  char **args = toys.optargs;

  // Lie to autoconf when it asks stupid questions, so configure regexes
  // that look for "GNU sed version %f" greater than some old buggy number
  // don't fail us for not matching their narrow expectations.
  if (toys.optflags & FLAG_version) {
    xprintf("This is not GNU sed version 9.0\n");
    return;
  }

  // Handling our own --version means we handle our own --help too.
  if (toys.optflags&FLAG_help) help_exit(0);

  // Parse pattern into commands.

  // If no -e or -f, first argument is the pattern.
  if (!TT.e && !TT.f) {
    if (!*toys.optargs) error_exit("no pattern");
    (TT.e = xzalloc(sizeof(struct arg_list)))->arg = *(args++);
  }

  // Option parsing infrastructure can't interlace "-e blah -f blah -e blah"
  // so handle all -e, then all -f. (At least the behavior's consistent.)

  for (al = TT.e; al; al = al->next) parse_pattern(&al->arg, strlen(al->arg));
  for (al = TT.f; al; al = al->next) do_lines(xopenro(al->arg), parse_pattern);
  parse_pattern(0, 0);
  dlist_terminate(TT.pattern);
  if (TT.nextlen) error_exit("no }");  

  TT.fdout = 1;
  TT.remember = xstrdup("");

  // Inflict pattern upon input files. Long version because !O_CLOEXEC
  loopfiles_rw(args, O_RDONLY|WARN_ONLY, 0, do_sed);

  if (!(toys.optflags & FLAG_i)) process_line(0, 0);

  // todo: need to close fd when done for TOYBOX_FREE?
}
