/* vi: set sw=4 ts=4 :
 * args.c - Command line argument parsing.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// Design goals:
//   Don't use getopt()
//   Don't permute original arguments.
//   handle --long gracefully "(noshort)a(along)b(blong1)(blong2)"
//   After each argument:
//       Note that pointer and long are always the same size, even on 64 bit.
//     : plus a string argument, keep most recent if more than one
//     * plus a string argument, appended to a list
//     ? plus a signed long argument (TODO: Bounds checking?)
//     @ plus an occurrence counter (which is a long)
//     | this is required.  If more than one marked, only one required.
//     (longopt)
//     +X enabling this enables X (switch on)
//     ~X enabling this disables X (switch off)
//       x~x means toggle x, I.E. specifying it again switches it off.
//     !X die with error if X already set (x!x die if x supplied twice)
//     [yz] needs at least one of y or z.
//   at the beginning:
//     + stop at first nonoption argument
//     ? return array of remaining arguments in first vararg
//     <0 at least # leftover arguments needed (default 0)
//     >9 at most # leftover arguments needed (default MAX_INT)
//     # don't show_usage() on unknown argument.
//     & first argument has imaginary dash (ala tar/ps)
//       If given twice, all arguments have imaginary dash

// Notes from getopt man page
//   - and -- cannot be arguments.
//     -- force end of arguments
//     - is a synonym for stdin in file arguments
//   -abc means -a -b -c

/* This uses a getopt-like option string, but not getopt() itself.
 * 
 *   Each option in options corresponds to a bit position in the return
 * value (last argument is (1<<0), the next to last is (1<<1) and so on.
 * If the option isn't seen in argv its bit is 0.  Options which have an
 * argument use the next vararg.  (So varargs used by options go from left to
 * right, but bits set by arguments go from right to left.)
 *
 * Example:
 *   get_optflags("ab:c:d", NULL, &bstring, &cstring);
 *   argv = ["command", "-b", "fruit", "-d"]
 *   flags = 5, bstring="fruit", cstring=NULL;
 */

struct opts {
	struct opts *next;
	char c;
	int type;
	int shift;
	void *arg;
};

struct getoptflagstate
{
	int argc;
	char *arg;
	struct opts *opts, *this;
	int noerror, nodash_now;
};

static struct getoptflagstate gof;

// Returns zero if it didn't consume the rest of the current -abcdef
static int gotflag(void)
{
	char *arg = NULL;
	int type;
	int ret = 0;

	// Did we recognize this option?
	if (!gof.this && !gof.noerror) error_exit("Unknown option %s\n", gof.arg);
	else toys.optflags |= 1 << gof.this->shift;

	// Does this option take an argument?
	gof.arg++;
	if (gof.this->type & 255) {
		// Make "tar xCjfv blah1 blah2 thingy" work like
		// "tar -x -C blah1 -j -f blah2 -v thingy"
		if (!gof.nodash_now && !*gof.arg) {
			gof.arg = toys.argv[++gof.argc];
			if (!gof.arg) error_exit("Missing argument");
		} else {
			arg = gof.arg;
			ret++;
		}
	} else gof.this = NULL;

	// If the last option had an argument, grab it.
	if (!gof.this)  return 0;
	type = gof.this->type & 255;
	if (!gof.arg && !(gof.arg = toys.argv[++gof.argc]))
		error_exit("Missing argument");
	if (type == ':') gof.this->arg = arg;
	else if (type == '*') {
		struct arg_list *temp, **list;
		list = (struct arg_list **)gof.this->arg;
		temp = xmalloc(sizeof(struct arg_list));
		temp->arg = arg;
		temp->next = *list;
		*list = temp;
	} else if (type == '?') {
	} else if (type == '@') {
	}

	return ret;
}

// Fill out toys.optflags and toys.optargs.  This isn't reentrant because
// we don't bzero(&gof, sizeof(gof));

void get_optflags(void)
{
	int stopearly = 0, optarg = 0, nodash = 0, minargs = 0, maxargs = INT_MAX;
	struct longopts {
		struct longopts *next;
		struct opts *opt;
		char *str;
		int len;
	} *longopts = NULL;
	long *nextarg = (long *)&toy;
	char *options = toys.which->options;

	if (options) {
		// Parse leading special behavior indicators
		for (;;) {
			if (*options == '+') stopearly++;
			else if (*options == '<') minargs=*(++options)-'0';
			else if (*options == '>') maxargs=*(++options)-'0';
			else if (*options == '#') gof.noerror++;
			else if (*options == '&') nodash++;
			else break;
			options++;
		}

		// Parse rest of opts into array
		while (*options) {

			// Allocate a new option entry when necessary
			if (!gof.this) {
				gof.this = xzalloc(sizeof(struct opts));
				gof.this->next = gof.opts;
				gof.opts = gof.this;
			}
			// Each option must start with (or an option character.  (Bare
			// longopts only come at the start of the string.)
			if (*options == '(') {
				char *end;
				struct longopts *lo = xmalloc(sizeof(struct longopts));

				// Find the end of the longopt
				for (end = ++options; *end && *end != ')'; end++);
				if (CFG_DEBUG && !*end) error_exit("Unterminated optstring");

				// Allocate and init a new struct longopts
				lo = xmalloc(sizeof(struct longopts));
				lo->next = longopts;
				lo->opt = gof.this;
				lo->str = options;
				lo->len = end-options;
				longopts = lo;
				options = end;

				// For leading longopts (with no corresponding short opt), note
				// that this option struct has been used.
				gof.this->shift++;

			// If this is the start of a new option that wasn't a longopt,

			} else if (index(":*?@", *options)) {
				gof.this->type |= *options;
				// Pointer and long guaranteed to be the same size by LP64.
				*(++nextarg) = 0;
				gof.this->arg = (void *)nextarg;
			} else if (*options == '|') {
			} else if (*options == '+') {
			} else if (*options == '~') {
			} else if (*options == '!') {
			} else if (*options == '[') {

			// At this point, we've hit the end of the previous option.  The
			// current character is the start of a new option.  If we've already
			// assigned an option to this struct, loop to allocate a new one.
			// (It'll get back here afterwards.)
			} else if(gof.this->shift || gof.this->c) {
				gof.this = NULL;
				continue;

			// Claim this option, loop to see what's after it.
			} else gof.this->c = *options;

			options++;
		}
	}

	// Initialize shift bits (have to calculate this ahead of time because
	// longopts jump into the middle of the list), and allocate space to
	// store optargs.
	gof.argc = 0;
	for (gof.this = gof.opts; gof.this; gof.this = gof.this->next)
		gof.this->shift = gof.argc++;
	toys.optargs = xzalloc(sizeof(char *)*(++gof.argc));

	// Iterate through command line arguments, skipping argv[0]
	for (gof.argc=1; toys.argv[gof.argc]; gof.argc++) {
		char *arg = toys.argv[gof.argc];

		// Parse this argument
		if (stopearly>1) goto notflag;

		gof.nodash_now = 0;

		// Various things with dashes
		if (*arg == '-') {

			// Handle -
			if (!arg[1]) goto notflag;
			arg++;
			if (*arg=='-') {
				struct longopts *lo;

				arg++;
				// Handle --
				if (!*arg) {
					stopearly += 2;
					goto notflag;
				}
				// Handle --longopt

				for (lo = longopts; lo; lo = lo->next) {
					if (!strncmp(arg, lo->str, lo->len)) {
						if (arg[lo->len]) {
							if (arg[lo->len]=='='
								&& (lo->opt->type & 255))
							{
								arg += lo->len;
							} else continue;

						// *options should be nul, this makes sure
						// that the while (*arg) loop terminates;
						} arg = options-1;
						gof.this = lo->opt;
						break;
					}
				}
				// Long option parsed, jump to option handling.
				gotflag();
				continue;
			}

		// Handle things that don't start with a dash.
		} else {
			if (nodash && (nodash>1 || gof.argc == 1)) gof.nodash_now = 1;
			else goto notflag;
		}

		// At this point, we have the args part of -args.  Loop through
		// each entry (could be -abc meaning -a -b -c)
		while (*arg) {
			// Identify next option char.
			for (gof.this = gof.opts; gof.this && *arg != gof.this->c;
					gof.this = gof.this->next);
			if (gotflag()) break;
			arg++;
		}
		continue;

		// Not a flag, save value in toys.optargs[]
notflag:
		if (stopearly) stopearly++;
		toys.optargs[optarg++] = toys.argv[gof.argc];
	}

	// Sanity check
	if (optarg<minargs) error_exit("Need %d arguments", minargs);
	if (optarg>maxargs) error_exit("Max %d arguments", maxargs);
}
