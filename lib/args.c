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
//     # plus a signed long argument (TODO: Bounds checking?)
//     @ plus an occurrence counter (which is a long)
//     (longopt)
//     | this is required.  If more than one marked, only one required.
//     ^ Stop parsing after encountering this argument
//
//     These modify other option letters (previously seen in string):
//       +X enabling this enables X (switch on)
//       ~X enabling this disables X (switch off)
//       !X die with error if X already set (x!x die if x supplied twice)
//       [yz] needs at least one of y or z.
//   at the beginning:
//     ^ stop at first nonoption argument
//     <0 at least # leftover arguments needed (default 0)
//     >9 at most # leftover arguments needed (default MAX_INT)
//     ? don't show_usage() on unknown argument.
//     & first argument has imaginary dash (ala tar/ps)
//       If given twice, all arguments have imaginary dash

// Notes from getopt man page
//   - and -- cannot be arguments.
//     -- force end of arguments
//     - is a synonym for stdin in file arguments
//   -abc means -a -b -c

/* This uses a getopt-like option string, but not getopt() itself.  We call
 * it the get_opt string.
 *
 * Each option in the get_opt string corresponds to a bit position in the
 * return value.  The rightmost argument is (1<<0), the next to last is (1<<1)
 * and so on.  If the option isn't seen in argv[], its bit remains 0.
 *
 * Options which have an argument fill in the corresponding slot in the global
 * union "this" (see generated/globals.h), which it treats as an array of longs
 * (note that sizeof(long)==sizeof(pointer) is guaranteed by LP64).
 *
 * You don't have to free the option strings, which point into the environment
 * space.  List objects should be freed by main() when command_main() returns.
 *
 * Example:
 *   Calling get_optflags() when toys.which->options="ab:c:d" and
 *   argv = ["command", "-b", "fruit", "-d", "walrus"] results in:
 *
 *     Changes to struct toys:
 *       toys.optflags = 5  (-b=4 | -d=1)
 *       toys.optargs[0]="walrus" (leftover argument)
 *       toys.optargs[1]=NULL (end of list)
 *       toys.optc=1 (there was 1 leftover argument)
 *
 *     Changes to union this:
 *       this[0]=NULL (because -c didn't get an argument this time)
 *       this[1]="fruit" (argument to -b)
 */

// Linked list of all known options (get_opt string is parsed into this).
struct opts {
	struct opts *next;
	long *arg;         // Pointer into union "this" to store arguments at.
	uint32_t edx[3];   // Flag mask to enable/disable/exclude.
	int c;             // Short argument character
	int flags;         // |=1, ^=2
	char type;         // Type of arguments to store
};

// State during argument parsing.
struct getoptflagstate
{
	int argc;
	char *arg;
	struct opts *opts, *this;
	int noerror, nodash_now, stopearly;
	uint32_t excludes;
};

// Parse one command line option.

static int gotflag(struct getoptflagstate *gof)
{
	int type;
	struct opts *opt = gof->this;

	// Did we recognize this option?
	if (!opt) {
		if (gof->noerror) return 1;
		error_exit("Unknown option %s", gof->arg);
	}
	toys.optflags |= opt->edx[0];
	toys.optflags &= ~opt->edx[1];
	gof->excludes = opt->edx[2];
	if (opt->flags&2) gof->stopearly=2;

	// Does this option take an argument?
	gof->arg++;
	type = opt->type;
	if (type) {

		// Handle "-xblah" and "-x blah", but also a third case: "abxc blah"
		// to make "tar xCjfv blah1 blah2 thingy" work like
		// "tar -x -C blah1 -j -f blah2 -v thingy"
		if (!gof->nodash_now && !gof->arg[0]) {
			gof->arg = toys.argv[++gof->argc];
			// TODO: The following line doesn't display --longopt correctly
			if (!gof->arg) error_exit("Missing argument to -%c",opt->c);
		}

		// Grab argument.
		if (!gof->arg && !(gof->arg = toys.argv[++(gof->argc)]))
			error_exit("Missing argument");
		if (type == ':') *(opt->arg) = (long)gof->arg;
		else if (type == '*') {
			struct arg_list **list;

			list = (struct arg_list **)opt->arg;
			while (*list) list=&((*list)->next);
			*list = xzalloc(sizeof(struct arg_list));
			(*list)->arg = gof->arg;
		} else if (type == '#') *(opt->arg) = atolx((char *)gof->arg);
		else if (type == '@') ++*(opt->arg);

		gof->arg = "";
	}

	gof->this = NULL;
	return 0;
}

// Fill out toys.optflags and toys.optargs.

static char *plustildenot = "+~!";
void get_optflags(void)
{
	int nodash = 0, minargs = 0, maxargs;
	struct longopts {
		struct longopts *next;
		struct opts *opt;
		char *str;
		int len;
	} *longopts = NULL;
	struct getoptflagstate gof;
	long *nextarg = (long *)&this, saveflags;
	char *options = toys.which->options;
	char *letters[]={"s",""};

	if (CFG_HELP) toys.exithelp++;
	// Allocate memory for optargs
	maxargs = 0;
	while (toys.argv[maxargs++]);
	toys.optargs = xzalloc(sizeof(char *)*maxargs);
	maxargs = INT_MAX;
	bzero(&gof, sizeof(struct getoptflagstate));

	// Parse option format
	if (options) {

		// Parse leading special behavior indicators
		for (;;) {
			if (*options == '^') gof.stopearly++;
			else if (*options == '<') minargs=*(++options)-'0';
			else if (*options == '>') maxargs=*(++options)-'0';
			else if (*options == '?') gof.noerror++;
			else if (*options == '&') nodash++;
			else break;
			options++;
		}

		if (!*options) gof.stopearly++;
		// Parse rest of opts into array
		while (*options) {
			char *temp;

			// Allocate a new option entry when necessary
			if (!gof.this) {
				gof.this = xzalloc(sizeof(struct opts));
				gof.this->next = gof.opts;
				gof.opts = gof.this;
				++*(gof.this->edx);
			}
			// Each option must start with "(" or an option character.  (Bare
			// longopts only come at the start of the string.)
			if (*options == '(') {
				char *end;
				struct longopts *lo = xmalloc(sizeof(struct longopts));

				// Find the end of the longopt
				for (end = ++options; *end && *end != ')'; end++);
				if (CFG_TOYBOX_DEBUG && !*end)
					error_exit("Bug1 in get_opt");

				// Allocate and init a new struct longopts
				lo = xmalloc(sizeof(struct longopts));
				lo->next = longopts;
				lo->opt = gof.this;
				lo->str = options;
				lo->len = end-options;
				longopts = lo;
				options = end;

				// Mark this as struct opt as used, even when no short opt.
				if (!gof.this->c) gof.this->c = -1;

			// If this is the start of a new option that wasn't a longopt,

			} else if (strchr(":*#@", *options)) {
				gof.this->type = *options;
			} else if (0 != (temp = strchr(plustildenot, *options))) {
				int i=0, idx = temp - plustildenot;
				struct opts *opt;

				if (!*++options && CFG_TOYBOX_DEBUG)
					error_exit("Bug2 in get_opt");
				// Find this option flag (in previously parsed struct opt)
				for (opt = gof.this; ; opt = opt->next) {
					if (CFG_TOYBOX_DEBUG && !opt) error_exit("Bug3 in get_opt");
					if (opt->c == *options) break;
					i++;
				}
				gof.this->edx[idx] |= 1<<i;

			} else if (*options == '[') {
			} else if (*options == '|') gof.this->flags |= 1;
			else if (*options == '^') gof.this->flags |= 2;

			// At this point, we've hit the end of the previous option.  The
			// current character is the start of a new option.  If we've already
			// assigned an option to this struct, loop to allocate a new one.
			// (It'll get back here afterwards and fall through to next else.)
			else if(gof.this->c) {
				gof.this = NULL;
				continue;

			// Claim this option, loop to see what's after it.
			} else gof.this->c = *options;

			options++;
		}
	}

	// Initialize enable/disable/exclude masks and pointers to store arguments.
	// (We have to calculate all this ahead of time because longopts jump into
	// the middle of the list.)
	gof.argc = 0;
	for (gof.this = gof.opts; gof.this; gof.this = gof.this->next) {
		int i;

		for (i=0;i<3;i++) gof.this->edx[i] <<= gof.argc;
		gof.argc++;
		if (gof.this->type) {
			gof.this->arg = (void *)nextarg;
			*(nextarg++) = 0;
		}
	}

	// Iterate through command line arguments, skipping argv[0]
	for (gof.argc=1; toys.argv[gof.argc]; gof.argc++) {
		gof.arg = toys.argv[gof.argc];
		gof.this = NULL;

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
					goto notflag;
				}
				// Handle --longopt

				for (lo = longopts; lo; lo = lo->next) {
					if (!strncmp(gof.arg, lo->str, lo->len)) {
						if (gof.arg[lo->len]) {
							if (gof.arg[lo->len]=='=' && lo->opt->type)
								gof.arg += lo->len;
							else continue;
						}
						// It's a match.
						gof.arg = "";
						gof.this = lo->opt;
						break;
					}
				}

				// Should we handle this --longopt as a non-option argument?
				if (!lo && gof.noerror) {
					gof.arg-=2;
					goto notflag;
				}

				// Long option parsed, handle option.
				gotflag(&gof);
				continue;
			}

		// Handle things that don't start with a dash.
		} else {
			if (nodash && (nodash>1 || gof.argc == 1)) gof.nodash_now = 1;
			else goto notflag;
		}

		// At this point, we have the args part of -args.  Loop through
		// each entry (could be -abc meaning -a -b -c)
		saveflags = toys.optflags;
		while (*gof.arg) {

			// Identify next option char.
			for (gof.this = gof.opts; gof.this; gof.this = gof.this->next)
				if (*gof.arg == gof.this->c) break;

			// Handle option char (advancing past what was used)
			if (gotflag(&gof) ) {
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
	if (toys.optc<minargs) {
		error_exit("Need%s %d argument%s", letters[!!(minargs-1)], minargs,
		letters[!(minargs-1)]);
	}
	if (toys.optc>maxargs)
		error_exit("Max %d argument%s", maxargs, letters[!(maxargs-1)]);
	if (CFG_HELP) toys.exithelp = 0;
}
