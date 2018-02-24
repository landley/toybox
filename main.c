/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

#ifndef TOYBOX_VERSION
#ifndef TOYBOX_VENDOR
#define TOYBOX_VENDOR ""
#endif
#define TOYBOX_VERSION "0.7.6"TOYBOX_VENDOR
#endif

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
char toybuf[4096], libbuf[4096];

struct toy_list *toy_find(char *name)
{
  int top, bottom, middle;

  if (!CFG_TOYBOX) return 0;

  // If the name starts with "toybox" accept that as a match.  Otherwise
  // skip the first entry, which is out of order.

  if (!strncmp(name,"toybox",6)) return toy_list;
  bottom = 1;

  // Binary search to find this command.

  top = ARRAY_LEN(toy_list)-1;
  for (;;) {
    int result;

    middle = (top+bottom)/2;
    if (middle<bottom || middle>top) return NULL;
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

static void unknown(char *name)
{
  toys.exitval = 127;
  toys.which = toy_list;
  error_exit("Unknown command %s", name);
}

// Setup toybox global state for this command.
static void toy_singleinit(struct toy_list *which, char *argv[])
{
  toys.which = which;
  toys.argv = argv;

  if (CFG_TOYBOX_I18N) setlocale(LC_CTYPE, "C.UTF-8");

  // Parse --help and --version for (almost) all commands
  if (CFG_TOYBOX_HELP_DASHDASH && !(which->flags & TOYFLAG_NOHELP) && argv[1]) {
    if (!strcmp(argv[1], "--help")) {
      if (CFG_TOYBOX && toys.which == toy_list && toys.argv[2])
        if (!(toys.which = toy_find(toys.argv[2]))) unknown(toys.argv[2]);
      show_help(stdout);
      xexit();
    }

    if (!strcmp(argv[1], "--version")) {
      xputs("toybox "TOYBOX_VERSION);
      xexit();
    }
  }

  if (NEED_OPTIONS && which->options) get_optflags();
  else {
    toys.optargs = argv+1;
    for (toys.optc = 0; toys.optargs[toys.optc]; toys.optc++);
  }
  toys.old_umask = umask(0);
  if (!(which->flags & TOYFLAG_UMASK)) umask(toys.old_umask);
  toys.signalfd--;
  toys.toycount = ARRAY_LEN(toy_list);
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

    if ((which->flags & TOYFLAG_NEEDROOT) && euid) help_exit("Not root");
  }

  // Free old toys contents (to be reentrant), but leave rebound if any
  // don't blank old optargs if our new argc lives in the old optargs.
  if (argv<toys.optargs || argv>toys.optargs+toys.optc) free(toys.optargs);
  memset(&toys, 0, offsetof(struct toy_context, rebound));
  if (oldwhich) memset(&this, 0, sizeof(this));

  // Continue to portion of init needed by standalone commands
  toy_singleinit(which, argv);
}

// Like exec() but runs an internal toybox command instead of another file.
// Only returns if it can't run command internally, otherwise exit() when done.
void toy_exec(char *argv[])
{
  struct toy_list *which;

  // Return if we can't find it (which includes no multiplexer case),
  if (!(which = toy_find(*argv))) return;

  // Return if stack depth getting noticeable (proxy for leaked heap, etc).

  // Compiler writers have decided subtracting char * is undefined behavior,
  // so convert to integers. (LP64 says sizeof(long)==sizeof(pointer).)
  if (!CFG_TOYBOX_NORECURSE)
    if (toys.stacktop && labs((long)toys.stacktop-(long)&which)>6000) return;

  // Return if we need to re-exec to acquire root via suid bit.
  if (toys.which && (which->flags&TOYFLAG_ROOTONLY) && toys.wasroot) return;

  // Run command
  toy_init(which, argv);
  if (toys.which) toys.which->toy_main();
  xexit();
}

// Multiplexer command, first argument is command to run, rest are args to that.
// If first argument starts with - output list of command install paths.
void toybox_main(void)
{
  static char *toy_paths[]={"usr/","bin/","sbin/",0};
  int i, len = 0;

  // fast path: try to exec immediately.
  // (Leave toys.which null to disable suid return logic.)
  if (toys.argv[1]) toy_exec(toys.argv+1);

  // For early error reporting
  toys.which = toy_list;

  if (toys.argv[1] && toys.argv[1][0] != '-') unknown(toys.argv[1]);

  // Output list of command.
  for (i=1; i<ARRAY_LEN(toy_list); i++) {
    int fl = toy_list[i].flags;
    if (fl & TOYMASK_LOCATION) {
      if (toys.argv[1]) {
        int j;
        for (j=0; toy_paths[j]; j++)
          if (fl & (1<<j)) len += printf("%s", toy_paths[j]);
      }
      len += printf("%s",toy_list[i].name);
      if (++len > 65) len = 0;
      xputc(len ? ' ' : '\n');
    }
  }
  xputc('\n');
}

int main(int argc, char *argv[])
{
  if (!*argv) return 127;

  // Snapshot stack location so we can detect recursion depth later.
  // This is its own block so probe doesn't permanently consume stack.
  else {
    int stack;

    toys.stacktop = &stack;
  }
  *argv = getbasename(*argv);

  // Up to and including Android M, bionic's dynamic linker added a handler to
  // cause a crash dump on SIGPIPE. That was removed in Android N, but adbd
  // was still setting the SIGPIPE disposition to SIG_IGN, and its children
  // were inheriting that. In Android O, adbd is fixed, but manually asking
  // for the default disposition is harmless, and it'll be a long time before
  // no one's using anything older than O!
  if (CFG_TOYBOX_ON_ANDROID) signal(SIGPIPE, SIG_DFL);

  // If nommu can't fork, special reentry path.
  // Use !stacktop to signal "vfork happened", both before and after xexec()
  if (!CFG_TOYBOX_FORK) {
    if (0x80 & **argv) {
      **argv &= 0x7f;
      toys.stacktop = 0;
    }
  }

  if (CFG_TOYBOX) {
    // Call the multiplexer, adjusting this argv[] to be its' argv[1].
    // (It will adjust it back before calling toy_exec().)
    toys.argv = argv-1;
    toybox_main();
  } else {
    // a single toybox command built standalone with no multiplexer
    toy_singleinit(toy_list, argv);
    toy_list->toy_main();
  }

  xexit();
}
