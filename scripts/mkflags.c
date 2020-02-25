// Take three word input lines on stdin and produce flag #defines to stdout.
// The three words on each input lnie are command name, option string with
// current config, option string from allyesconfig. The three are space
// separated and the last two are in double quotes.

// This is intentionally crappy code because we control the inputs. It leaks
// memory like a sieve and segfaults if malloc returns null, but does the job.

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

struct flag {
  struct flag *next;
  char *command;
  struct flag *lopt;
};

int chrtype(char c)
{
  // Does this populate a GLOBALS() variable?
  if (strchr("?&^-:#|@*; %", c)) return 1;

  // Is this followed by a numeric argument in optstr?
  if (strchr("=<>", c)) return 2;

  return 0;
}

// replace chopped out USE_BLAH() sections with low-ascii characters
// showing how many flags got skipped

char *mark_gaps(char *flags, char *all)
{
  char *n, *new, c;
  int bare = 1;

  // Shell feeds in " " for blank args, leading space not meaningful.
  while (isspace(*flags)) flags++;
  while (isspace(*all)) all++;

  n = new = strdup(all);
  while (*all) {
    // --longopt parentheticals dealt with as a unit
    if (*all == '(') {
      int len = 0;

      while (all[len++] != ')');
      if (strncmp(flags, all, len)) {
        // bare longopts need their own skip placeholders
        if (bare) *(new++) = 1;
      } else {
        memcpy(new, all, len);
        new += len;
        flags += len;
      }
      all += len;
      continue;
    }
    c = *(all++);
    if (bare) bare = chrtype(c);
    if (*flags == c) {
      *(new++) = c;
      flags++;
      continue;
    }

    c = chrtype(c);
    if (!c) *(new++) = 1;
    else if (c==2) while (isdigit(*all)) all++;
  }
  *new = 0;

  return n;
}

// Break down a command string into linked list of "struct flag".

struct flag *digest(char *string)
{
  struct flag *list = NULL;
  char *err = string, c;

  while (*string) {
    // Groups must be at end.
    if (*string == '[') break;

    // Longopts
    if (*string == '(') {
      struct flag *new = calloc(sizeof(struct flag), 1);

      new->command = ++string;

      // Attach longopt to previous short opt, if any.
      if (list && list->command) {
        new->next = list->lopt;
        list->lopt = new;
      } else {
        struct flag *blank = calloc(sizeof(struct flag), 1);

        blank->next = list;
        blank->lopt = new;
        list = blank;
      }
      // An empty longopt () would break this.
      while (*++string != ')') if (*string == '-') *string = '_';
      *(string++) = 0;
      continue;
    }

    c = chrtype(*string);
    if (c == 1) string++;
    else if (c == 2) {
      if (string[1]=='-') string++;
      if (!isdigit(string[1])) {
        fprintf(stderr, "%c without number in '%s'", *string, err);
        exit(1);
      }
      while (isdigit(*++string)) {
        if (!list) {
           string++;
           break;
        }
      }
    } else {
      struct flag *new = calloc(sizeof(struct flag), 1);

      new->command = string++;
      new->next = list;
      list = new;
    }
  }

  return list;
}

// Parse C-style octal escape
void octane(char *from)
{
  unsigned char *to = (void *)from;

  while (*from) {
    if (*from == '\\') {
      *to = 0;
      while (isdigit(*++from)) *to = (8**to)+*from-'0';
      to++;
    } else *to++ = *from++;
  }
  *to = 0;
}

int main(int argc, char *argv[])
{
  char command[256], flags[1024], allflags[1024];
  char *out, *outbuf = malloc(1024*1024);

  // Yes, the output buffer is 1 megabyte with no bounds checking.
  // See "intentionally crappy", above.
  if (!(out = outbuf)) return 1;

  printf("#undef FORCED_FLAG\n#undef FORCED_FLAGLL\n"
    "#ifdef FORCE_FLAGS\n#define FORCED_FLAG 1\n#define FORCED_FLAGLL 1ULL\n"
    "#else\n#define FORCED_FLAG 0\n#define FORCED_FLAGLL 0\n#endif\n\n");

  for (;;) {
    struct flag *flist, *aflist, *offlist;
    char *mgaps = 0;
    unsigned bit;

    *command = *flags = *allflags = 0;
    bit = fscanf(stdin, "%255s \"%1023[^\"]\" \"%1023[^\"]\"\n",
                    command, flags, allflags);
    octane(flags);
    octane(allflags);

    if (getenv("DEBUG"))
      fprintf(stderr, "command=%s, flags=%s, allflags=%s\n",
        command, flags, allflags);

    if (!*command) break;
    if (bit != 3) {
      fprintf(stderr, "\nError in %s (see generated/flags.raw)\n", command);
      exit(1);
    }

    bit = 0;
    printf("// %s %s %s\n", command, flags, allflags);
    if (*flags != ' ') mgaps = mark_gaps(flags, allflags);
    else if (*allflags != ' ') mgaps = allflags;
    // If command disabled, use allflags for OLDTOY()
    printf("#undef OPTSTR_%s\n#define OPTSTR_%s ", command, command);
    if (mgaps) printf("\"%s\"\n", mgaps);
    else printf("0\n");
    if (mgaps != allflags) free(mgaps);

    flist = digest(flags);
    offlist = aflist = digest(allflags);

    printf("#ifdef CLEANUP_%s\n#undef CLEANUP_%s\n#undef FOR_%s\n",
           command, command, command);

    while (offlist) {
      char *s = (char []){0, 0, 0, 0};

      if (!offlist->command) s = offlist->lopt->command;
      else {
        *s = *offlist->command;
        if (127 < (unsigned char)*s) sprintf(s, "X%02X", 127&*s);
      }
      printf("#undef FLAG_%s\n", s);
      offlist = offlist->next;
    }
    printf("#endif\n\n");

    sprintf(out, "#ifdef FOR_%s\n#ifndef TT\n#define TT this.%s\n#endif\n",
            command, command);
    out += strlen(out);

    while (aflist) {
      char *llstr = bit>30 ? "LL" : "", *s = (char []){0, 0, 0, 0};
      int enabled = 0;

      // Output flag macro for bare longopts
      if (!aflist->command) {
        s = aflist->lopt->command;
        if (flist && flist->lopt &&
            !strcmp(flist->lopt->command, aflist->lopt->command)) enabled++;
      // Output normal flag macro
      } else {
        *s = *aflist->command;
        if (127 < (unsigned char)*s) sprintf(s, "X%02X", 127&*s);
        if (flist && flist->command && *aflist->command == *flist->command)
          enabled++;
      }
      out += sprintf(out, "#define FLAG_%s (%s%s<<%d)\n",
                       s, enabled ? "1" : "FORCED_FLAG", llstr, bit++);
      aflist = aflist->next;
      if (enabled) flist = flist->next;
    }
    out = stpcpy(out, "#endif\n\n");
  }

  if (fflush(0) && ferror(stdout)) return 1;

  out = outbuf;
  while (*out) {
    int i = write(1, outbuf, strlen(outbuf));

    if (i<0) return 1;
    out += i;
  }

  return 0;
}
