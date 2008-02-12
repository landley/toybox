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
//     | this is required.  If more than one marked, only one required.
//     (longopt)
//     +X enabling this enables X (switch on)
//     ~X enabling this disables X (switch off)
//       x~x means toggle x, I.E. specifying it again switches it off.
//     !X die with error if X already set (x!x die if x supplied twice)
//     [yz] needs at least one of y or z.
//   at the beginning:
//     + stop at first nonoption argument
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

/* This uses a getopt-like option string, but not getopt() itself.
 *
 * Each option in options corresponds to a bit position in the return
 * value (last argument is (1<<0), the next to last is (1<<1) and so on).
 * If the option isn't seen in argv[] its bit is 0.
 *
 * Options which have an argument fill in the corresponding slot in the global
 * toys.command struct, which it treats as an array of longs (note that
 * sizeof(long)==sizeof(pointer) is guaranteed by LP64).
 *
 * You don't have to free the option strings, which point into the environment
 * space.  List list objects should be freed by main() when command_main()
 * returns.
 *
 * Example:
 *   get_optflags() when toys.which->options="ab:c:d"
 *   argv = ["command", "-b", "fruit", "-d"]
 *   flags = 5, toy[0]=NULL, toy[1]="fruit";
 */

//
struct opts {
	struct opts *next;
	char c;
	int type;
	int shift;
	long *arg;
};

static struct getoptflagstate
{
	int argc;
	char *arg;
	struct opts *opts, *this;
	int noerror, nodash_now;
} gof;

static void gotflag(void)
{
	int type;

	// Did we recognize this option?
	if (!gof.this && !gof.noerror) error_exit("Unknown option %s", gof.arg);
	else toys.optflags |= 1 << gof.this->shift;

	// Does this option take an argument?
	gof.arg++;
	type = gof.this->type & 255;
	if (type) {

		// Handle "-xblah" and "-x blah", but also a third case: "abxc blah"
		// to make "tar xCjfv blah1 blah2 thingy" work like
		// "tar -x -C blah1 -j -f blah2 -v thingy"
		if (!gof.nodash_now && !*gof.arg) {
			gof.arg = toys.argv[++gof.argc];
			if (!gof.arg) error_exit("Missing argument");
		}

		// Grab argument.
		if (!gof.arg && !(gof.arg = toys.argv[++gof.argc]))
			error_exit("Missing argument");
		if (type == ':') *(gof.this->arg) = (long)gof.arg;
		else if (type == '*') {
			struct arg_list *temp, **list;
			list = (struct arg_list **)gof.this->arg;
			temp = xmalloc(sizeof(struct arg_list));
			temp->arg = gof.arg;
			temp->next = *list;
			*list = temp;
		} else if (type == '#') *(gof.this->arg) = atolx((char *)gof.arg);
		else if (type == '@') {
		}

		gof.arg = "";
	}

	gof.this = NULL;
}

// Fill out toys.optflags and toys.optargs.  This isn't reentrant because
// we don't bzero(&gof, sizeof(gof));

void get_optflags(void)
{
	int stopearly = 0, nodash = 0, minargs = 0, maxargs;
	struct longopts {
		struct longopts *next;
		struct opts *opt;
		char *str;
		int len;
	} *longopts = NULL;
	long *nextarg = (long *)&this;
	char *options = toys.which->options;

	if (CFG_HELP) toys.exithelp++;
	// Allocate memory for optargs
	maxargs = 0;
	while (toys.argv[maxargs++]);
	toys.optargs = xzalloc(sizeof(char *)*maxargs);
	maxargs = INT_MAX;

	// Parse option format
	if (options) {
		// Parse leading special behavior indicators
		for (;;) {
			if (*options == '+') stopearly++;
			else if (*options == '<') minargs=*(++options)-'0';
			else if (*options == '>') maxargs=*(++options)-'0';
			else if (*options == '?') gof.noerror++;
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
				if (CFG_TOYBOX_DEBUG && !*end)
					error_exit("Unterminated optstring");

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

			} else if (index(":*#@", *options)) {
				gof.this->type |= *options;
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

	// Initialize shift bits and pointers to store arguments.  (We have to
	// calculate this ahead of time because longopts jump into the middle of
	// the list.)
	gof.argc = 0;
	for (gof.this = gof.opts; gof.this; gof.this = gof.this->next) {
		gof.this->shift = gof.argc++;
		if (gof.this->type & 255) {
			gof.this->arg = (void *)nextarg;
			*(nextarg++) = 0;
		}
	}

	// Iterate through command line arguments, skipping argv[0]
	for (gof.argc=1; toys.argv[gof.argc]; gof.argc++) {
		gof.arg = toys.argv[gof.argc];
		gof.this = NULL;

		// Parse this argument
		if (stopearly>1) goto notflag;

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
					stopearly += 2;
					goto notflag;
				}
				// Handle --longopt

				for (lo = longopts; lo; lo = lo->next) {
					if (!strncmp(gof.arg, lo->str, lo->len)) {
						if (gof.arg[lo->len]) {
							if (gof.arg[lo->len]=='='
								&& (lo->opt->type & 255))
							{
								gof.arg += lo->len;
							} else continue;
						}
						// It's a match.
						gof.arg = "";
						gof.this = lo->opt;
						break;
					}
				}

				// Long option parsed, handle option.
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
		while (*gof.arg) {

			// Identify next option char.
			for (gof.this = gof.opts; gof.this; gof.this = gof.this->next)
				if (*gof.arg == gof.this->c) break;

			// Handle option char (advancing past what was used)
			gotflag();
		}
		continue;

		// Not a flag, save value in toys.optargs[]
notflag:
		if (stopearly) stopearly++;
		toys.optargs[toys.optc++] = toys.argv[gof.argc];
	}

	// Sanity check
	if (toys.optc<minargs)
		error_exit("Need %d argument%s", minargs, minargs ? "s" : "");
	if (toys.optc>maxargs)
		error_exit("Max %d argument%s", maxargs, maxargs ? "s" : "");
	if (CFG_HELP) toys.exithelp = 0;
}

// Loop through files listed on the command line

static int dofileargs(char ***files, int fd, int iswrite)
{
	char *filename = *((*files)++);
	static int flags[] = {O_RDONLY, O_CREAT|O_TRUNC, O_RDWR};

	if (fd != -1) close(fd);

	for (;;) {

		// Are there no more files?
		if (!*filename)
			return (fd == -1) ? iswrite : -1;

		// A filename of "-" means stdin.
		if (*filename == '-' && !filename[1]) return 0;

		fd = xcreate(filename, flags[iswrite], 0777);
	}
}

int readfileargs(char ***files, int fd)
{
	return dofileargs(files, fd, 0);
}

int writefileargs(char ***files, int fd)
{
	return dofileargs(files, fd, 1);
}
