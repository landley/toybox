/* sort.c - put input lines into order
 *
 * Copyright 2004, 2008 Rob Landley <rob@landley.net>
 *
 * See http://opengroup.org/onlinepubs/007904975/utilities/sort.html

USE_SORT(NEWTOY(sort, USE_SORT_FLOAT("g")USE_SORT_BIG("S:T:m" "o:k*t:xbMcszdfi") "run", TOYFLAG_USR|TOYFLAG_BIN))

config SORT
  bool "sort"
  default y
  help
    usage: sort [-run] [FILE...]

    Sort all lines of text from input files (or stdin) to stdout.

    -r	reverse
    -u	unique lines only
    -n	numeric order (instead of alphabetical)

config SORT_BIG
  bool "SuSv3 options (Support -ktcsbdfiozM)"
  default y
  depends on SORT
  help
    usage: sort [-bcdfiMsz] [-k#[,#[x]] [-t X]] [-o FILE]

    -b	ignore leading blanks (or trailing blanks in second part of key)
    -c	check whether input is sorted
    -d	dictionary order (use alphanumeric and whitespace chars only)
    -f	force uppercase (case insensitive sort)
    -i	ignore nonprinting characters
    -M	month sort (jan, feb, etc).
    -x	Hexadecimal numerical sort
    -s	skip fallback sort (only sort with keys)
    -z	zero (null) terminated input
    -k	sort by "key" (see below)
    -t	use a key separator other than whitespace
    -o	output to FILE instead of stdout

    Sorting by key looks at a subset of the words on each line.  -k2
    uses the second word to the end of the line, -k2,2 looks at only
    the second word, -k2,4 looks from the start of the second to the end
    of the fourth word.  Specifying multiple keys uses the later keys as
    tie breakers, in order.  A type specifier appended to a sort key
    (such as -2,2n) applies only to sorting that key.

config SORT_FLOAT
  bool
  default y
  depends on SORT_BIG && TOYBOX_FLOAT
  help
    usage: sort [-g]

    -g	general numeric sort (double precision with nan and inf)
*/

#define FOR_sort
#include "toys.h"

GLOBALS(
  char *key_separator;
  struct arg_list *raw_keys;
  char *outfile;
  char *ignore1, ignore2;   // GNU compatability NOPs for -S and -T.

  void *key_list;
  int linecount;
  char **lines;
)

// The sort types are n, g, and M.
// u, c, s, and z apply to top level only, not to keys.
// b at top level implies bb.
// The remaining options can be applied to search keys.

#define FLAG_bb (1<<31)  // Ignore trailing blanks

struct sort_key
{
  struct sort_key *next_key;  // linked list
  unsigned range[4];          // start word, start char, end word, end char
  int flags;
};

// Copy of the part of this string corresponding to a key/flags.

static char *get_key_data(char *str, struct sort_key *key, int flags)
{
  int start=0, end, len, i, j;

  // Special case whole string, so we don't have to make a copy

  if(key->range[0]==1 && !key->range[1] && !key->range[2] && !key->range[3]
    && !(flags&(FLAG_b&FLAG_d&FLAG_f&FLAG_i&FLAG_bb))) return str;

  // Find start of key on first pass, end on second pass

  len = strlen(str);
  for (j=0; j<2; j++) {
    if (!key->range[2*j]) end=len;

    // Loop through fields
    else {
      end=0;
      for (i=1; i < key->range[2*j]+j; i++) {

        // Skip leading blanks
        if (str[end] && !TT.key_separator)
          while (isspace(str[end])) end++;

        // Skip body of key
        for (; str[end]; end++) {
          if (TT.key_separator) {
            if (str[end]==*TT.key_separator) break;
          } else if (isspace(str[end])) break;
        }
      }
    }
    if (!j) start=end;
  }

  // Key with explicit separator starts after the separator
  if (TT.key_separator && str[start]==*TT.key_separator) start++;

  // Strip leading and trailing whitespace if necessary
  if (flags&FLAG_b) while (isspace(str[start])) start++;
  if (flags&FLAG_bb) while (end>start && isspace(str[end-1])) end--;

  // Handle offsets on start and end
  if (key->range[3]) {
    end += key->range[3]-1;
    if (end>len) end=len;
  }
  if (key->range[1]) {
    start += key->range[1]-1;
    if (start>len) start=len;
  }

  // Make the copy
  if (end<start) end=start;
  str = xstrndup(str+start, end-start);

  // Handle -d
  if (flags&FLAG_d) {
    for (start = end = 0; str[end]; end++)
      if (isspace(str[end]) || isalnum(str[end])) str[start++] = str[end];
    str[start] = 0;
  }

  // Handle -i
  if (flags&FLAG_i) {
    for (start = end = 0; str[end]; end++)
      if (isprint(str[end])) str[start++] = str[end];
    str[start] = 0;
  }

  // Handle -f
  if (flags*FLAG_f) for(i=0; str[i]; i++) str[i] = toupper(str[i]);

  return str;
}

// append a sort_key to key_list.

static struct sort_key *add_key(void)
{
  void **stupid_compiler = &TT.key_list;
  struct sort_key **pkey = (struct sort_key **)stupid_compiler;

  while (*pkey) pkey = &((*pkey)->next_key);
  return *pkey = xzalloc(sizeof(struct sort_key));
}

// Perform actual comparison
static int compare_values(int flags, char *x, char *y)
{
  int ff = flags & (FLAG_n|FLAG_g|FLAG_M|FLAG_x);

  // Ascii sort
  if (!ff) return strcmp(x, y);

  if (CFG_SORT_FLOAT && ff == FLAG_g) {
    char *xx,*yy;
    double dx = strtod(x,&xx), dy = strtod(y,&yy);
    int xinf, yinf;

    // not numbers < NaN < -infinity < numbers < +infinity

    if (x==xx) return y==yy ? 0 : -1;
    if (y==yy) return 1;

    // Check for isnan
    if (dx!=dx) return (dy!=dy) ? 0 : -1;
    if (dy!=dy) return 1;

    // Check for infinity.  (Could underflow, but avoids needing libm.)
    xinf = (1.0/dx == 0.0);
    yinf = (1.0/dy == 0.0);
    if (xinf) {
      if(dx<0) return (yinf && dy<0) ? 0 : -1;
      return (yinf && dy>0) ? 0 : 1;
    }
    if (yinf) return dy<0 ? 1 : -1;

    return dx>dy ? 1 : (dx<dy ? -1 : 0);
  } else if (CFG_SORT_BIG && ff == FLAG_M) {
    struct tm thyme;
    int dx;
    char *xx,*yy;

    xx = strptime(x,"%b",&thyme);
    dx = thyme.tm_mon;
    yy = strptime(y,"%b",&thyme);
    if (!xx) return !yy ? 0 : -1;
    else if (!yy) return 1;
    else return dx==thyme.tm_mon ? 0 : dx-thyme.tm_mon;

  } else if (CFG_SORT_BIG && ff == FLAG_x) {
    return strtol(x, NULL, 16)-strtol(y, NULL, 16);
  // This has to be ff == FLAG_n
  } else {
    // Full floating point version of -n
    if (CFG_SORT_FLOAT) {
      double dx = atof(x), dy = atof(y);

      return dx>dy ? 1 : (dx<dy ? -1 : 0);
    // Integer version of -n for tiny systems
    } else return atoi(x)-atoi(y);
  }
}

// Callback from qsort(): Iterate through key_list and perform comparisons.
static int compare_keys(const void *xarg, const void *yarg)
{
  int flags = toys.optflags, retval = 0;
  char *x, *y, *xx = *(char **)xarg, *yy = *(char **)yarg;
  struct sort_key *key;

  if (CFG_SORT_BIG) {
    for (key=(struct sort_key *)TT.key_list; !retval && key;
       key = key->next_key)
    {
      flags = key->flags ? key->flags : toys.optflags;

      // Chop out and modify key chunks, handling -dfib

      x = get_key_data(xx, key, flags);
      y = get_key_data(yy, key, flags);

      retval = compare_values(flags, x, y);

      // Free the copies get_key_data() made.

      if (x != xx) free(x);
      if (y != yy) free(y);

      if (retval) break;
    }
  } else retval = compare_values(flags, xx, yy);

  // Perform fallback sort if necessary
  if (!retval && !(CFG_SORT_BIG && (toys.optflags&FLAG_s))) {
    retval = strcmp(xx, yy);
    flags = toys.optflags;
  }

  return retval * ((flags&FLAG_r) ? -1 : 1);
}

// Callback from loopfiles to handle input files.
static void sort_read(int fd, char *name)
{
  // Read each line from file, appending to a big array.

  for (;;) {
    char * line = (CFG_SORT_BIG && (toys.optflags&FLAG_z))
             ? get_rawline(fd, NULL, 0) : get_line(fd);

    if (!line) break;

    // handle -c here so we don't allocate more memory than necessary.
    if (CFG_SORT_BIG && (toys.optflags&FLAG_c)) {
      int j = (toys.optflags&FLAG_u) ? -1 : 0;

      if (TT.lines && compare_keys((void *)&TT.lines, &line)>j)
        error_exit("%s: Check line %d\n", name, TT.linecount);
      free(TT.lines);
      TT.lines = (char **)line;
    } else {
      if (!(TT.linecount&63))
        TT.lines = xrealloc(TT.lines, sizeof(char *)*(TT.linecount+64));
      TT.lines[TT.linecount] = line;
    }
    TT.linecount++;
  }
}

void sort_main(void)
{
  int idx, fd = 1;

  // Open output file if necessary.
  if (CFG_SORT_BIG && TT.outfile)
    fd = xcreate(TT.outfile, O_CREAT|O_TRUNC|O_WRONLY, 0666);

  // Parse -k sort keys.
  if (CFG_SORT_BIG && TT.raw_keys) {
    struct arg_list *arg;

    for (arg = TT.raw_keys; arg; arg = arg->next) {
      struct sort_key *key = add_key();
      char *temp;
      int flag;

      idx = 0;
      temp = arg->arg;
      while (*temp) {
        // Start of range
        key->range[2*idx] = (unsigned)strtol(temp, &temp, 10);
        if (*temp=='.')
          key->range[(2*idx)+1] = (unsigned)strtol(temp+1, &temp, 10);

        // Handle flags appended to a key type.
        for (;*temp;temp++) {
          char *temp2, *optlist;

          // Note that a second comma becomes an "Unknown key" error.

          if (*temp==',' && !idx++) {
            temp++;
            break;
          }

          // Which flag is this?

          optlist = toys.which->options;
          temp2 = strchr(optlist, *temp);
          flag = (1<<(optlist-temp2+strlen(optlist)-1));

          // Was it a flag that can apply to a key?

          if (!temp2 || flag>FLAG_b
            || (flag&(FLAG_u|FLAG_c|FLAG_s|FLAG_z)))
          {
            error_exit("Unknown key option.");
          }
          // b after , means strip _trailing_ space, not leading.
          if (idx && flag==FLAG_b) flag = FLAG_bb;
          key->flags |= flag;
        }
      }
    }
  }

  // global b flag strips both leading and trailing spaces
  if (toys.optflags&FLAG_b) toys.optflags |= FLAG_bb;

  // If no keys, perform alphabetic sort over the whole line.
  if (CFG_SORT_BIG && !TT.key_list) add_key()->range[0] = 1;

  // Open input files and read data, populating TT.lines[TT.linecount]
  loopfiles(toys.optargs, sort_read);

  // The compare (-c) logic was handled in sort_read(),
  // so if we got here, we're done.
  if (CFG_SORT_BIG && (toys.optflags&FLAG_c)) goto exit_now;

  // Perform the actual sort
  qsort(TT.lines, TT.linecount, sizeof(char *), compare_keys);

  // handle unique (-u)
  if (toys.optflags&FLAG_u) {
    int jdx;

    for (jdx=0, idx=1; idx<TT.linecount; idx++) {
      if (!compare_keys(&TT.lines[jdx], &TT.lines[idx]))
        free(TT.lines[idx]);
      else TT.lines[++jdx] = TT.lines[idx];
    }
    if (TT.linecount) TT.linecount = jdx+1;
  }

  // Output result
  for (idx = 0; idx<TT.linecount; idx++) {
    char *s = TT.lines[idx];
    xwrite(fd, s, strlen(s));
    if (CFG_TOYBOX_FREE) free(s);
    xwrite(fd, "\n", 1);
  }

exit_now:
  if (CFG_TOYBOX_FREE) {
    if (fd != 1) close(fd);
    free(TT.lines);
  }
}
