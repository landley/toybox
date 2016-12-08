// Take three word input lines on stdin (the three space separated words are
// command name, option string with current config, option string from
// allyesconfig; space separated, the last two are and double quotes)
// and produce flag #defines to stdout.

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
  if (strchr("?&^-:#|@*; ", c)) return 1;
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
      *flags++;
      continue;
    }

    c = chrtype(c);
    if (!c) *(new++) = 1;
    else if (c==2) while (isdigit(*all)) all++;
  }
  *new = 0;

  return n;
}

// Break down a command string into struct flag list.

struct flag *digest(char *string)
{
  struct flag *list = NULL;
  char *err = string;

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

    if (strchr("?&^-:#|@*; ", *string)) string++;
    else if (strchr("=<>", *string)) {
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

int main(int argc, char *argv[])
{
  char command[256], flags[1023], allflags[1024];
  char *out, *outbuf = malloc(1024*1024);

  // Yes, the output buffer is 1 megabyte with no bounds checking.
  // See "intentionally crappy", above.
  if (!(out = outbuf)) return 1;

  printf("#undef FORCED_FLAG\n#undef FORCED_FLAGLL\n"
    "#ifdef FORCE_FLAGS\n#define FORCED_FLAG 1\n#define FORCED_FLAGLL 1LL\n"
    "#else\n#define FORCED_FLAG 0\n#define FORCED_FLAGLL 0\n#endif\n\n");

  for (;;) {
    struct flag *flist, *aflist, *offlist;
    char *mgaps = 0;
    unsigned bit;

    *command = *flags = *allflags = 0;
    bit = fscanf(stdin, "%255s \"%1023[^\"]\" \"%1023[^\"]\"\n",
                    command, flags, allflags);

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
      struct flag *f = offlist->lopt;
      while (f) {
        printf("#undef FLAG_%s\n", f->command);
        f = f->next;
      }
      if (offlist->command) printf("#undef FLAG_%c\n", *offlist->command);
      offlist = offlist->next;
    }
    printf("#endif\n\n");

    sprintf(out, "#ifdef FOR_%s\n#ifndef TT\n#define TT this.%s\n#endif\n",
            command, command);
    out += strlen(out);

    while (aflist) {
      char *llstr = bit>31 ? "LL" : "";

      // Output flag macro for bare longopts
      if (aflist->lopt) {
        if (flist && flist->lopt &&
            !strcmp(flist->lopt->command, aflist->lopt->command))
        {
          sprintf(out, "#define FLAG_%s (1%s<<%d)\n", flist->lopt->command,
            llstr, bit);
          flist->lopt = flist->lopt->next;
        } else sprintf(out, "#define FLAG_%s (FORCED_FLAG%s<<%d)\n",
                       aflist->lopt->command, llstr, bit);
        aflist->lopt = aflist->lopt->next;
        if (!aflist->command) {
          aflist = aflist->next;
          bit++;
          if (flist) flist = flist->next;
        }
      // Output normal flag macro
      } else if (aflist->command) {
        if (flist && flist->command && *aflist->command == *flist->command) {
          if (aflist->command)
            sprintf(out, "#define FLAG_%c (1%s<<%d)\n", *aflist->command,
              llstr, bit);
          flist = flist->next;
        } else sprintf(out, "#define FLAG_%c (FORCED_FLAG%s<<%d)\n",
                       *aflist->command, llstr, bit);
        bit++;
        aflist = aflist->next;
      }
      out += strlen(out);
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
