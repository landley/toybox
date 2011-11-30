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
//     # plus a signed long argument
//       {LOW,HIGH} - allowed range TODO
//     @ plus an occurrence counter (which is a long)
//     (longopt)
//     | this is required.  If more than one marked, only one required.
//     ^ Stop parsing after encountering this argument
//
//     These modify other option letters (previously seen in string):
//       +X enabling this enables X (switch on)
//       ~X enabling this disables X (switch off)
//       !X die with error if X already set (x!x die if x supplied twice)
//       [yz] needs at least one of y or z. TODO
//   at the beginning:
//     ^ stop at first nonoption argument
//     <0 at least # leftover arguments needed (default 0)
//     >9 at most # leftover arguments needed (default MAX_INT)
//     ? Allow unknown arguments (pass them through to command).
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
	struct opts *opts, *this;
	struct longopts *longopts;
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

	// Set flags
	toys.optflags |= opt->edx[0];
	toys.optflags &= ~opt->edx[1];
	gof->excludes = opt->edx[2];
	if (opt->flags&2) gof->stopearly=2;

	// Does this option take an argument?
	gof->arg++;
	type = opt->type;
	if (type) {
		char *arg = gof->arg;

		// Handle "-xblah" and "-x blah", but also a third case: "abxc blah"
		// to make "tar xCjfv blah1 blah2 thingy" work like
		// "tar -x -C blah1 -j -f blah2 -v thingy"

		if (gof->nodash_now || !arg[0]) arg = toys.argv[++gof->argc];
		// TODO: The following line doesn't display --longopt correctly
		if (!arg) error_exit("Missing argument to -%c", opt->c);

		if (type == ':') *(opt->arg) = (long)arg;
		else if (type == '*') {
			struct arg_list **list;

			list = (struct arg_list **)opt->arg;
			while (*list) list=&((*list)->next);
			*list = xzalloc(sizeof(struct arg_list));
			(*list)->arg = arg;
		} else if (type == '#') *(opt->arg) = atolx((char *)arg);
		else if (type == '@') ++*(opt->arg);

		if (!gof->nodash_now) gof->arg = "";
	}

	gof->this = NULL;
	return 0;
}

// Fill out toys.optflags and toys.optargs.

void parse_optflaglist(struct getoptflagstate *gof)
{
	char *options = toys.which->options, *plustildenot = "+~!";
	long *nextarg = (long *)&this;
	struct opts *new = 0;

	// Parse option format
	bzero(gof, sizeof(struct getoptflagstate));
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

	// Parse the rest of the option characters into a linked list
    // of options with attributes.

	if (!*options) gof->stopearly++;
	while (*options) {
		char *temp;

		// Allocate a new list entry when necessary
		if (!new) {
			new = xzalloc(sizeof(struct opts));
			new->next = gof->opts;
			gof->opts = new;
			++*(new->edx);
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
			lo->next = gof->longopts;
			lo->opt = new;
			lo->str = options;
			lo->len = end-options;
			gof->longopts = lo;
			options = end;

			// Mark this struct opt as used, even when no short opt.
			if (!new->c) new->c = -1;

		// If this is the start of a new option that wasn't a longopt,

		} else if (strchr(":*#@", *options)) {
			if (CFG_TOYBOX_DEBUG && new->type)
				error_exit("Bug4 in get_opt");
			new->type = *options;
		} else if (0 != (temp = strchr(plustildenot, *options))) {
			int i=0, idx = temp - plustildenot;
			struct opts *opt;

			if (!*++options && CFG_TOYBOX_DEBUG)
				error_exit("Bug2 in get_opt");
			// Find this option flag (in previously parsed struct opt)
			for (opt = new; ; opt = opt->next) {
				if (CFG_TOYBOX_DEBUG && !opt) error_exit("Bug3 in get_opt");
				if (opt->c == *options) break;
				i++;
			}
			new->edx[idx] |= 1<<i;
		} else if (*options == '[') {
		} else if (*options == '|') new->flags |= 1;
		else if (*options == '^') new->flags |= 2;

		// At this point, we've hit the end of the previous option.  The
		// current character is the start of a new option.  If we've already
		// assigned an option to this struct, loop to allocate a new one.
		// (It'll get back here afterwards and fall through to next else.)
		else if (new->c) {
			new = NULL;
			continue;

		// Claim this option, loop to see what's after it.
		} else new->c = *options;

		options++;
	}

	// Initialize enable/disable/exclude masks and pointers to store arguments.
	// (We have to calculate all this ahead of time because longopts jump into
	// the middle of the list.)
	int pos = 0;
	for (new = gof->opts; new; new = new->next) {
		int i;

		for (i=0;i<3;i++) new->edx[i] <<= pos;
		pos++;
		if (new->type) {
			new->arg = (void *)nextarg;
			*(nextarg++) = 0;
		}
	}
}

void get_optflags(void)
{
	struct getoptflagstate gof;
	long saveflags;
	char *letters[]={"s",""};

	if (CFG_HELP) toys.exithelp++;
	// Allocate memory for optargs
	saveflags = 0;
	while (toys.argv[saveflags++]);
	toys.optargs = xzalloc(sizeof(char *)*saveflags);

	parse_optflaglist(&gof);

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

				for (lo = gof.longopts; lo; lo = lo->next) {
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
			if (gof.nodash && (gof.nodash>1 || gof.argc == 1))
				gof.nodash_now = 1;
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
	if (toys.optc<gof.minargs) {
		error_exit("Need%s %d argument%s", letters[!!(gof.minargs-1)],
				gof.minargs, letters[!(gof.minargs-1)]);
	}
	if (toys.optc>gof.maxargs)
		error_exit("Max %d argument%s", gof.maxargs, letters[!(gof.maxargs-1)]);
	if (CFG_HELP) toys.exithelp = 0;
}
