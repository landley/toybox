/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// Populate toy_list[].

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name, opts, flags) {#name, name##_main, OPTSTR_##name, flags},
#define OLDTOY(name, oldname, flags) \
  {#name, oldname##_main, OPTSTR_##oldname, flags},

struct toy_list toy_list[] = {
#include "generated/newtoys.h"
};

// global context for this command.

struct toy_context toys;
union global_union this;
char *toybox_version = TOYBOX_VERSION, toybuf[4096], libbuf[4096];

struct toy_list *toy_find(char *name)
{
  int top, bottom, middle;

  if (!CFG_TOYBOX || strchr(name, '/')) return 0;

  // Multiplexer name works as prefix, else skip first entry (it's out of order)
  if (!toys.which && strstart(&name, toy_list->name)) return toy_list;
  bottom = 1;

  // Binary search to find this command.
  top = ARRAY_LEN(toy_list)-1;
  for (;;) {
    int result;

    middle = (top+bottom)/2;
    if (middle<bottom || middle>top) return 0;
    result = strcmp(name,toy_list[middle].name);
    if (!result) return toy_list+middle;
    if (result<0) top = --middle;
    else bottom = ++middle;
  }
}

// Figure out whether or not anything is using the option parsing logic,
// because the compiler can't figure out whether or not to optimize it away
// on its' own.  NEED_OPTIONS becomes a constant allowing if() to optimize
// stuff out via dead code elimination.

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name, opts, flags) opts ||
#define OLDTOY(name, oldname, flags) OPTSTR_##oldname ||
static const int NEED_OPTIONS =
#include "generated/newtoys.h"
0;  // Ends the opts || opts || opts...

// Populate help text array

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name,opt,flags) HELP_##name "\0"
#if CFG_TOYBOX
#define OLDTOY(name,oldname,flags) "\xff" #oldname "\0"
#else
#define OLDTOY(name, oldname, flags) HELP_##oldname "\0"
#endif

#include "generated/help.h"
static char *help_data =
#include "generated/newtoys.h"
;

void show_help(FILE *out, int full)
{
  int i = toys.which-toy_list;
  char *s, *ss;

  if (!(full&2))
    fprintf(out, "Toybox %s" USE_TOYBOX(" multicall binary")
                 ": https://landley.net/toybox"
                 USE_TOYBOX(" (see toybox --help)") "\n\n", toybox_version);

  if (CFG_TOYBOX_HELP) {
    for (;;) {
      s = help_data;
      while (i--) s += strlen(s) + 1;
      // If it's an alias, restart search for real name
      if (*s != 255) break;
      i = toy_find(++s)-toy_list;
    }

    if (full) fprintf(out, "%s\n", s);
    else {
      strstart(&s, "usage: ");
      for (ss = s; *ss && *ss!='\n'; ss++);
      fprintf(out, "%.*s\n", (int)(ss-s), s);
    }
  }
}

static void unknown(char *name)
{
  toys.exitval = 127;
  toys.which = toy_list;
  help_exit("Unknown command %s", name);
}

// Parse --help and --version for (almost) all commands
void check_help(char **arg)
{
  if (!CFG_TOYBOX_HELP_DASHDASH || !*arg || (toys.which->flags&TOYFLAG_NOHELP))
    return;

  if (!strcmp(*arg, "--help")) {
    if (CFG_TOYBOX && toys.which == toy_list && arg[1])
      if (!(toys.which = toy_find(arg[1]))) unknown(arg[1]);
    show_help(stdout, 1);
    xexit();
  }

  if (!strcmp(*arg, "--version")) {
    xprintf("toybox %s\n", toybox_version);
    xexit();
  }
}

// Setup toybox global state for this command.
void toy_singleinit(struct toy_list *which, char *argv[])
{
  toys.which = which;
  toys.argv = argv;
  toys.toycount = ARRAY_LEN(toy_list);

  if (NEED_OPTIONS && which->options) get_optflags();
  else {
    check_help(toys.optargs = argv+1);
    for (toys.optc = 0; toys.optargs[toys.optc]; toys.optc++);
  }

  if (!(CFG_TOYBOX && which == toy_list) && !(which->flags & TOYFLAG_NOFORK)) {
    toys.old_umask = umask(0);
    if (!(which->flags & TOYFLAG_UMASK)) umask(toys.old_umask);

    // Try user's locale, but merge in the en_US.UTF-8 locale's character
    // type data if the user's locale isn't UTF-8. (We can't merge in C.UTF-8
    // because that locale doesn't exist on macOS.)
    setlocale(LC_CTYPE, "");
    if (strcmp("UTF-8", nl_langinfo(CODESET)))
      uselocale(newlocale(LC_CTYPE_MASK, "en_US.UTF-8", NULL));

    setvbuf(stdout, 0, (which->flags & TOYFLAG_LINEBUF) ? _IOLBF : _IONBF, 0);
  }
}

// Full init needed by multiplexer or reentrant calls, calls singleinit at end
void toy_init(struct toy_list *which, char *argv[])
{
  void *oldwhich = toys.which;

  // Drop permissions for non-suid commands.

  if (CFG_TOYBOX_SUID) {
    if (!toys.which) toys.which = toy_list;

    uid_t uid = getuid(), euid = geteuid();

    if (!(which->flags & TOYFLAG_STAYROOT)) {
      if (uid != euid) {
        if (setuid(uid)) perror_exit("setuid %d->%d", euid, uid); // drop root
        euid = uid;
        toys.wasroot++;
      }
    } else if (CFG_TOYBOX_DEBUG && uid && which != toy_list)
      error_msg("Not installed suid root");

    if ((which->flags & TOYFLAG_NEEDROOT) && euid) {
      check_help(argv+1);
      help_exit("Not root");
    }
  }

  // Free old toys contents (to be reentrant), but leave rebound if any
  // don't blank old optargs if our new argc lives in the old optargs.
  if (argv<toys.optargs || argv>toys.optargs+toys.optc) free(toys.optargs);
  memset(&toys, 0, offsetof(struct toy_context, rebound));
  if (oldwhich) memset(&this, 0, sizeof(this));

  // Continue to portion of init needed by standalone commands
  toy_singleinit(which, argv);
}

// Run an internal toybox command.
// Only returns if it can't run command internally, otherwise xexit() when done.
static void toy_exec_which(struct toy_list *which, char *argv[])
{
  // Return if we can't find it (which includes no multiplexer case),
  if (!which || (which->flags&TOYFLAG_NOFORK)) return;

  // Return if stack depth getting noticeable (proxy for leaked heap, etc).

  // Compiler writers have decided subtracting char * is undefined behavior,
  // so convert to integers. (LP64 says sizeof(long)==sizeof(pointer).)
  // Signed typecast so stack growth direction is irrelevant: we're measuring
  // the distance between two pointers on the same stack, hence the labs().
  if (!CFG_TOYBOX_NORECURSE && toys.stacktop)
    if (labs((long)toys.stacktop-(long)&which)>6000) return;

  // Return if we need to re-exec to acquire root via suid bit.
  if (toys.which && (which->flags&TOYFLAG_ROOTONLY) && toys.wasroot) return;

  // Run command
  toy_init(which, argv);
  if (toys.which) toys.which->toy_main();
  xexit();
}

// Lookup internal toybox command to run via argv[0]
void toy_exec(char *argv[])
{
  toy_exec_which(toy_find(*argv), argv);
}

// Multiplexer command, first argument is command to run, rest are args to that.
// If first argument starts with - output list of command install paths.
void toybox_main(void)
{
  char *toy_paths[] = {"usr/", "bin/", "sbin/", 0}, *s = toys.argv[1];
  int i, len = 0;
  unsigned width = 80;

  // fast path: try to exec immediately.
  // (Leave toys.which null to disable suid return logic.)
  // Try dereferencing one layer of symlink
  while (s) {
    struct toy_list *tl = toy_find(basename(s));

    if (tl==toy_list && s!=toys.argv[1]) unknown(basename(s));
    toy_exec_which(toy_find(basename(s)), toys.argv+1);
    s = (0<readlink(s, libbuf, sizeof(libbuf))) ? libbuf : 0;
  }

  // For early error reporting
  toys.which = toy_list;

  if (toys.argv[1] && strcmp(toys.argv[1], "--long")) unknown(toys.argv[1]);

  // Output list of commands.
  terminal_size(&width, 0);
  for (i = 1; i<ARRAY_LEN(toy_list); i++) {
    int fl = toy_list[i].flags;
    if (fl & TOYMASK_LOCATION) {
      if (toys.argv[1]) {
        int j;
        for (j = 0; toy_paths[j]; j++)
          if (fl & (1<<j)) len += printf("%s", toy_paths[j]);
      }
      len += printf("%s",toy_list[i].name);
      if (++len > width-15) len = 0;
      xputc(len ? ' ' : '\n');
    }
  }
  xputc('\n');
}

int main(int argc, char *argv[])
{
  // don't segfault if our environment is crazy
  if (!*argv) return 127;

  // Snapshot stack location so we can detect recursion depth later.
  // Nommu has special reentry path, !stacktop = "vfork/exec self happened"
  if (!CFG_TOYBOX_FORK && (0x80 & **argv)) **argv &= 0x7f;
  else {
    int stack_start;  // here so probe var won't permanently eat stack

    toys.stacktop = &stack_start;
  }

  // Android before O had non-default SIGPIPE, 7 years = remove in Sep 2024.
  if (CFG_TOYBOX_ON_ANDROID) signal(SIGPIPE, SIG_DFL);

  if (CFG_TOYBOX) {
    // Call the multiplexer with argv[] as its arguments so it can toy_find()
    toys.argv = argv-1;
    toybox_main();
  } else {
    // single command built standalone with no multiplexer is first list entry
    toy_singleinit(toy_list, argv);
    toy_list->toy_main();
  }

  xexit();
}
