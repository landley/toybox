/* args.c - Command line argument parsing.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// Design goals:
//   Don't use getopt() out of libc.
//   Don't permute original arguments (screwing up ps/top output).
//   Integrated --long options "(noshort)a(along)b(blong1)(blong2)"

/* This uses a getopt-like option string, but not getopt() itself. We call
 * it the get_opt string.
 *
 * Each option in the get_opt string corresponds to a bit position in the
 * return value. The rightmost argument is (1<<0), the next to last is (1<<1)
 * and so on. If the option isn't seen in argv[], its bit remains 0.
 *
 * Options which have an argument fill in the corresponding slot in the global
 * union "this" (see generated/globals.h), which it treats as an array of longs
 * (note that sizeof(long)==sizeof(pointer) is guaranteed by LP64).
 *
 * You don't have to free the option strings, which point into the environment
 * space. List objects should be freed by main() when command_main() returns.
 *
 * Example:
 *   Calling get_optflags() when toys.which->options="ab:c:d" and
 *   argv = ["command", "-b", "fruit", "-d", "walrus"] results in:
 *
 *     Changes to struct toys:
 *       toys.optflags = 5 (I.E. 0101 so -b = 4 | -d = 1)
 *       toys.optargs[0] = "walrus" (leftover argument)
 *       toys.optargs[1] = NULL (end of list)
 *       toys.optc = 1 (there was 1 leftover argument)
 *
 *     Changes to union this:
 *       this[0]=NULL (because -c didn't get an argument this time)
 *       this[1]="fruit" (argument to -b)
 */

// Enabling TOYBOX_DEBUG in .config adds syntax checks to option string parsing
// which aren't needed in the final code (your option string is hardwired and
// should be correct when you ship), but are useful for development.

// What you can put in a get_opt string:
//   Any otherwise unused character (all letters, unprefixed numbers) specify
//   an option that sets a flag. The bit value is the same as the binary digit
//   if you string the option characters together in order.
//   So in "abcdefgh" a = 128, h = 1
//
//   Suffixes specify that this option takes an argument (stored in GLOBALS):
//       Note that pointer and long are always the same size, even on 64 bit.
//     : plus a string argument, keep most recent if more than one
//     * plus a string argument, appended to a list
//     # plus a signed long argument
//       <LOW     - die if less than LOW
//       >HIGH    - die if greater than HIGH
//       =DEFAULT - value if not specified
//     - plus a signed long argument defaulting to negative (say + for positive)
//     . plus a double precision floating point argument (with CFG_TOYBOX_FLOAT)
//       Chop this option out with USE_TOYBOX_FLOAT() in option string
//       Same <LOW>HIGH=DEFAULT as #
//     @ plus an occurrence counter (which is a long)
//     (longopt)
//     | this is required. If more than one marked, only one required.
//     ; long option's argument is optional (can only be supplied with --opt=)
//     ^ Stop parsing after encountering this argument
//    " " (space char) the "plus an argument" must be separate
//        I.E. "-j 3" not "-j3". So "kill -stop" != "kill -s top"
//
//   At the beginning of the get_opt string (before any options):
//     ^ stop at first nonoption argument
//     <0 die if less than # leftover arguments (default 0)
//     >9 die if > # leftover arguments (default MAX_INT)
//     ? Allow unknown arguments (pass them through to command).
//     & first argument has imaginary dash (ala tar/ps)
//       If given twice, all arguments have imaginary dash
//
//   At the end: [groups] of previously seen options
//     - Only one in group (switch off)    [-abc] means -ab=-b, -ba=-a, -abc=-c
//     + Synonyms (switch on all)          [+abc] means -ab=-abc, -c=-abc
//     ! More than one in group is error   [!abc] means -ab calls error_exit()
//       primarily useful if you can switch things back off again.

// Notes from getopt man page
//   - and -- cannot be arguments.
//     -- force end of arguments
//     - is a synonym for stdin in file arguments
//   -abcd means -a -b -c -d (but if -b takes an argument, then it's -a -b cd)

// Linked list of all known options (option string parsed into this).
// Hangs off getoptflagstate, freed at end of option parsing.
struct opts {
  struct opts *next;
  long *arg;         // Pointer into union "this" to store arguments at.
  int c;             // Argument character to match
  int flags;         // |=1, ^=2
  unsigned dex[3];   // which bits to disable/enable/exclude in toys.optflags
  char type;         // Type of arguments to store union "this"
  union {
    long l;
    FLOAT f;
  } val[3];          // low, high, default - range of allowed values
};

// linked list of long options. (Hangs off getoptflagstate, free at end of
// option parsing, details about flag to set and global slot to fill out
// stored in related short option struct, but if opt->c = -1 the long option
// is "bare" (has no corresponding short option).
struct longopts {
  struct longopts *next;
  struct opts *opt;
  char *str;
  int len;
};

// State during argument parsing.
struct getoptflagstate
{
  int argc, minargs, maxargs, nodash;
  char *arg;
  struct opts *opts;
  struct longopts *longopts;
  int noerror, nodash_now, stopearly;
  unsigned excludes, requires;
};

// Use getoptflagstate to parse parse one command line option from argv
static int gotflag(struct getoptflagstate *gof, struct opts *opt)
{
  int type;

  // Did we recognize this option?
  if (!opt) {
    if (gof->noerror) return 1;
    error_exit("Unknown option %s", gof->arg);
  }

  // Might enabling this switch off something else?
  if (toys.optflags & opt->dex[0]) {
    struct opts *clr;
    unsigned i = 1;

    // Forget saved argument for flag we switch back off
    for (clr=gof->opts, i=1; clr; clr = clr->next, i<<=1)
      if (clr->arg && (i & toys.optflags & opt->dex[0])) *clr->arg = 0;
    toys.optflags &= ~opt->dex[0];
  }

  // Set flags
  toys.optflags |= opt->dex[1];
  gof->excludes |= opt->dex[2];
  if (opt->flags&2) gof->stopearly=2;

  if (toys.optflags & gof->excludes) {
    struct opts *bad;
    unsigned i = 1;

    for (bad=gof->opts, i=1; ;bad = bad->next, i<<=1) {
      if (opt == bad || !(i & toys.optflags)) continue;
      if (toys.optflags & bad->dex[2]) break;
    }
    error_exit("No '%c' with '%c'", opt->c, bad->c);
  }

  // Does this option take an argument?
  if (!gof->arg) {
    if (opt->flags & 8) return 0;
    gof->arg = "";
  } else gof->arg++;
  type = opt->type;

  if (type == '@') ++*(opt->arg);
  else if (type) {
    char *arg = gof->arg;

    // Handle "-xblah" and "-x blah", but also a third case: "abxc blah"
    // to make "tar xCjfv blah1 blah2 thingy" work like
    // "tar -x -C blah1 -j -f blah2 -v thingy"

    if (gof->nodash_now || (!arg[0] && !(opt->flags & 8)))
      arg = toys.argv[++gof->argc];
    if (!arg) {
      char *s = "Missing argument to ";
      struct longopts *lo;

      if (opt->c != -1) error_exit("%s-%c", s, opt->c);

      for (lo = gof->longopts; lo->opt != opt; lo = lo->next);
      error_exit("%s--%.*s", s, lo->len, lo->str);
    }

    if (type == ':') *(opt->arg) = (long)arg;
    else if (type == '*') {
      struct arg_list **list;

      list = (struct arg_list **)opt->arg;
      while (*list) list=&((*list)->next);
      *list = xzalloc(sizeof(struct arg_list));
      (*list)->arg = arg;
    } else if (type == '#' || type == '-') {
      long l = atolx(arg);
      if (type == '-' && !ispunct(*arg)) l*=-1;
      if (l < opt->val[0].l) error_exit("-%c < %ld", opt->c, opt->val[0].l);
      if (l > opt->val[1].l) error_exit("-%c > %ld", opt->c, opt->val[1].l);

      *(opt->arg) = l;
    } else if (CFG_TOYBOX_FLOAT && type == '.') {
      FLOAT *f = (FLOAT *)(opt->arg);

      *f = strtod(arg, &arg);
      if (opt->val[0].l != LONG_MIN && *f < opt->val[0].f)
        error_exit("-%c < %lf", opt->c, (double)opt->val[0].f);
      if (opt->val[1].l != LONG_MAX && *f > opt->val[1].f)
        error_exit("-%c > %lf", opt->c, (double)opt->val[1].f);
    }

    if (!gof->nodash_now) gof->arg = "";
  }

  return 0;
}

// Parse this command's options string into struct getoptflagstate, which
// includes a struct opts linked list in reverse order (I.E. right-to-left)
void parse_optflaglist(struct getoptflagstate *gof)
{
  char *options = toys.which->options;
  long *nextarg = (long *)&this;
  struct opts *new = 0;
  int idx;

  // Parse option format string
  memset(gof, 0, sizeof(struct getoptflagstate));
  gof->maxargs = INT_MAX;
  if (!options) return;

  // Parse leading special behavior indicators
  for (;;) {
    if (*options == '^') gof->stopearly++;
    else if (*options == '<') gof->minargs=*(++options)-'0';
    else if (*options == '>') gof->maxargs=*(++options)-'0';
    else if (*options == '?') gof->noerror++;
    else if (*options == '&') gof->nodash++;
    else break;
    options++;
  }

  // Parse option string into a linked list of options with attributes.

  if (!*options) gof->stopearly++;
  while (*options) {
    char *temp;

    // Option groups come after all options are defined
    if (*options == '[') break;

    // Allocate a new list entry when necessary
    if (!new) {
      new = xzalloc(sizeof(struct opts));
      new->next = gof->opts;
      gof->opts = new;
      new->val[0].l = LONG_MIN;
      new->val[1].l = LONG_MAX;
    }
    // Each option must start with "(" or an option character.  (Bare
    // longopts only come at the start of the string.)
    if (*options == '(' && new->c != -1) {
      char *end;
      struct longopts *lo;

      // Find the end of the longopt
      for (end = ++options; *end && *end != ')'; end++);
      if (CFG_TOYBOX_DEBUG && !*end) error_exit("(longopt) didn't end");

      // init a new struct longopts
      lo = xmalloc(sizeof(struct longopts));
      lo->next = gof->longopts;
      lo->opt = new;
      lo->str = options;
      lo->len = end-options;
      gof->longopts = lo;
      options = ++end;

      // Mark this struct opt as used, even when no short opt.
      if (!new->c) new->c = -1;

      continue;

    // If this is the start of a new option that wasn't a longopt,

    } else if (strchr(":*#@.-", *options)) {
      if (CFG_TOYBOX_DEBUG && new->type)
        error_exit("multiple types %c:%c%c", new->c, new->type, *options);
      new->type = *options;
    } else if (-1 != (idx = stridx("|^ ;", *options))) new->flags |= 1<<idx;
    // bounds checking
    else if (-1 != (idx = stridx("<>=", *options))) {
      if (new->type == '#') {
        long l = strtol(++options, &temp, 10);
        if (temp != options) new->val[idx].l = l;
      } else if (CFG_TOYBOX_FLOAT && new->type == '.') {
        FLOAT f = strtod(++options, &temp);
        if (temp != options) new->val[idx].f = f;
      } else if (CFG_TOYBOX_DEBUG) error_exit("<>= only after .#");
      options = --temp;

    // At this point, we've hit the end of the previous option.  The
    // current character is the start of a new option.  If we've already
    // assigned an option to this struct, loop to allocate a new one.
    // (It'll get back here afterwards and fall through to next else.)
    } else if (new->c) {
      new = 0;
      continue;

    // Claim this option, loop to see what's after it.
    } else new->c = *options;

    options++;
  }

  // Initialize enable/disable/exclude masks and pointers to store arguments.
  // (This goes right to left so we need the whole list before we can start.)
  idx = 0;
  for (new = gof->opts; new; new = new->next) {
    unsigned u = 1<<idx++;

    if (new->c == 1) new->c = 0;
    new->dex[1] = u;
    if (new->flags & 1) gof->requires |= u;
    if (new->type) {
      new->arg = (void *)nextarg;
      *(nextarg++) = new->val[2].l;
    }
  }

  // Parse trailing group indicators
  while (*options) {
    unsigned bits = 0;

    if (CFG_TOYBOX_DEBUG && *options != '[') error_exit("trailing %s", options);

    idx = stridx("-+!", *++options);
    if (CFG_TOYBOX_DEBUG && idx == -1) error_exit("[ needs +-!");
    if (CFG_TOYBOX_DEBUG && (options[1] == ']' || !options[1]))
      error_exit("empty []");

    // Don't advance past ] but do process it once in loop.
    while (*options++ != ']') {
      struct opts *opt;
      int i;

      if (CFG_TOYBOX_DEBUG && !*options) error_exit("[ without ]");
      // Find this option flag (in previously parsed struct opt)
      for (i=0, opt = gof->opts; ; i++, opt = opt->next) {
        if (*options == ']') {
          if (!opt) break;
          if (bits&(1<<i)) opt->dex[idx] |= bits&~(1<<i);
        } else {
          if (CFG_TOYBOX_DEBUG && !opt)
            error_exit("[] unknown target %c", *options);
          if (opt->c == *options) {
            bits |= 1<<i;
            break;
          }
        }
      }
    }
  }
}

// Fill out toys.optflags, toys.optargs, and this[] from toys.argv

void get_optflags(void)
{
  struct getoptflagstate gof;
  struct opts *catch;
  long saveflags;
  char *letters[]={"s",""};

  // Option parsing is a two stage process: parse the option string into
  // a struct opts list, then use that list to process argv[];

  toys.exithelp++;
  // Allocate memory for optargs
  saveflags = 0;
  while (toys.argv[saveflags++]);
  toys.optargs = xzalloc(sizeof(char *)*saveflags);

  parse_optflaglist(&gof);

  // Iterate through command line arguments, skipping argv[0]
  for (gof.argc=1; toys.argv[gof.argc]; gof.argc++) {
    gof.arg = toys.argv[gof.argc];
    catch = NULL;

    // Parse this argument
    if (gof.stopearly>1) goto notflag;

    gof.nodash_now = 0;

    // Various things with dashes
    if (*gof.arg == '-') {

      // Handle -
      if (!gof.arg[1]) goto notflag;
      gof.arg++;
      if (*gof.arg=='-') {
        struct longopts *lo;

        gof.arg++;
        // Handle --
        if (!*gof.arg) {
          gof.stopearly += 2;
          continue;
        }

        // do we match a known --longopt?
        for (lo = gof.longopts; lo; lo = lo->next) {
          if (!strncmp(gof.arg, lo->str, lo->len)) {
            if (!gof.arg[lo->len]) gof.arg = 0;
            else if (gof.arg[lo->len] == '=' && lo->opt->type)
              gof.arg += lo->len;
            else continue;
            // It's a match.
            catch = lo->opt;
            break;
          }
        }

        // Should we handle this --longopt as a non-option argument?
        if (!lo && gof.noerror) {
          gof.arg -= 2;
          goto notflag;
        }

        // Long option parsed, handle option.
        gotflag(&gof, catch);
        continue;
      }

    // Handle things that don't start with a dash.
    } else {
      if (gof.nodash && (gof.nodash>1 || gof.argc == 1)) gof.nodash_now = 1;
      else goto notflag;
    }

    // At this point, we have the args part of -args.  Loop through
    // each entry (could be -abc meaning -a -b -c)
    saveflags = toys.optflags;
    while (*gof.arg) {

      // Identify next option char.
      for (catch = gof.opts; catch; catch = catch->next)
        if (*gof.arg == catch->c)
          if (!((catch->flags&4) && gof.arg[1])) break;

      // Handle option char (advancing past what was used)
      if (gotflag(&gof, catch) ) {
        toys.optflags = saveflags;
        gof.arg = toys.argv[gof.argc];
        goto notflag;
      }
    }
    continue;

    // Not a flag, save value in toys.optargs[]
notflag:
    if (gof.stopearly) gof.stopearly++;
    toys.optargs[toys.optc++] = toys.argv[gof.argc];
  }

  // Sanity check
  if (toys.optc<gof.minargs)
    error_exit("Need%s %d argument%s", letters[!!(gof.minargs-1)],
      gof.minargs, letters[!(gof.minargs-1)]);
  if (toys.optc>gof.maxargs)
    error_exit("Max %d argument%s", gof.maxargs, letters[!(gof.maxargs-1)]);
  if (gof.requires && !(gof.requires & toys.optflags)) {
    struct opts *req;
    char needs[32], *s = needs;

    for (req = gof.opts; req; req = req->next)
      if (req->flags & 1) *(s++) = req->c;
    *s = 0;

    error_exit("Needs %s-%s", s[1] ? "one of " : "", needs);
  }
  toys.exithelp = 0;

  if (CFG_TOYBOX_FREE) {
    llist_traverse(gof.opts, free);
    llist_traverse(gof.longopts, free);
  }
}
