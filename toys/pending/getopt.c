/* getopt.c - Parse command-line options
 *
 * Copyright 2019 The Android Open Source Project

USE_GETOPT(NEWTOY(getopt, "^a(alternative)n:(name)o:(options)l*(long)(longoptions)Tu", TOYFLAG_USR|TOYFLAG_BIN))

config GETOPT
  bool "getopt"
  default n
  help
    usage: getopt [OPTIONS] [--] ARG...

    Parse command-line options for use in shell scripts.

    -a	Allow long options starting with a single -.
    -l OPTS	Specify long options.
    -n NAME	Command name for error messages.
    -o OPTS	Specify short options.
    -T	Test whether this is a modern getopt.
    -u	Output options unquoted.
*/

#define FOR_getopt
#include "toys.h"

GLOBALS(
  struct arg_list *l;
  char *o, *n;
)

static void out(char *s)
{
  if (FLAG(u)) printf(" %s", s);
  else {
    printf(" '");
    for (; *s; s++) {
      if (*s == '\'') printf("'\\''");
      else putchar(*s);
    }
    printf("'");
  }
}

static char *parse_long_opt(void *data, char *str, int len)
{
  struct option **lopt_ptr = data, *lopt = *lopt_ptr;

  // Trailing : or :: means this option takes a required or optional argument.
  // no_argument = 0, required_argument = 1, optional_argument = 2.
  for (lopt->has_arg = 0; len>0 && str[len-1] == ':'; lopt->has_arg++) len--;
  if (!len || lopt->has_arg>2) return str;

  lopt->name = xstrndup(str, len);

  (*lopt_ptr)++;
  return 0;
}

void getopt_main(void)
{
  int argc = toys.optc+1;
  char **argv = xzalloc(sizeof(char *)*(argc+1));
  struct option *lopts = xzalloc(sizeof(struct option)*argc), *lopt = lopts;
  int i = 0, j = 0, ch;

  if (FLAG(T)) {
    toys.exitval = 4;
    return;
  }

  comma_args(TT.l, &lopt, "bad -l", parse_long_opt);
  argv[j++] = TT.n ? TT.n : "getopt";

  // Legacy mode: don't quote output and take the first argument as OPTSTR.
  if (!FLAG(o)) {
    toys.optflags |= FLAG_u;
    TT.o = toys.optargs[i++];
    if (!TT.o) error_exit("no OPTSTR");
    --argc;
  }

  while (i<toys.optc) argv[j++] = toys.optargs[i++];

  // BSD getopts don't honor argv[0] (for -n), so handle errors ourselves.
  opterr = 0;
  optind = 1;
  while ((ch = (FLAG(a)?getopt_long_only:getopt_long)(argc, argv, TT.o,
          lopts, &i)) != -1) {
    if (ch == '?') {
      fprintf(stderr, "%s: invalid option '%c'\n", argv[0], optopt);
      toys.exitval = 1;
    } else if (!ch) {
      printf(" --%s", lopts[i].name);
      if (lopts[i].has_arg) out(optarg ? optarg : "");
    } else {
      printf(" -%c", ch);
      if (optarg) out(optarg);
    }
  }

  printf(" --");
  for (; optind<argc; optind++) out(argv[optind]);
  printf("\n");
}
