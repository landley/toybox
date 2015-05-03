// Take three word input lines on stdin (the three space separated words are
// command name, option string with current config, option string from
// allyesconfig; space separated, the last two are and double quotes)
// and produce flag #defines to stdout.

// This is intentionally crappy code because we control the inputs. It leaks
// memory like a sieve and segfaults if malloc returns null, but does the job.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct flag {
  struct flag *next;
  char *command;
  struct flag *lopt;
};

// replace chopped out USE_BLAH() sections with low-ascii characters
// showing how many flags got skipped

char *mark_gaps(char *flags, char *all)
{
  char *n, *new, c;

  // Shell feeds in " " for blank args, leading space not meaningful.
  while (isspace(*flags)) flags++;
  while (isspace(*all)) all++;

  n = new = strdup(all);
  while (*all) {
    if (*flags == *all) {
      *(new++) = *(all++);
      *flags++;
      continue;
    }

    c = *(all++);
    if (strchr("?&^-:#|@*; ", c));
    else if (strchr("=<>", c)) while (isdigit(*all)) all++;
    else if (c == '(') while(*(all++) != ')');
    else *(new++) = 1;
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

  printf("#ifdef FORCE_FLAGS\n#define FORCED_FLAG 1\n"
         "#else\n#define FORCED_FLAG 0\n#endif\n\n");

  for (;;) {
    struct flag *flist, *aflist, *offlist;
    char *gaps, *mgaps, c;
    unsigned bit;

    *command = *flags = *allflags = 0;
    bit = fscanf(stdin, "%255s \"%1023[^\"]\" \"%1023[^\"]\"\n",
                    command, flags, allflags);

    if (getenv("DEBUG"))
      fprintf(stderr, "command=%s, flags=%s, allflags=%s\n",
        command, flags, allflags);

    if (!*command) break;
    if (bit != 3) {
      fprintf(stderr, "\nError in %s (duplicate command?)\n", command);
      exit(1);
    }

    bit = 0;
    printf("// %s %s %s\n", command, flags, allflags);
    mgaps = mark_gaps(flags, allflags);
    for (gaps = mgaps; *gaps == 1; gaps++);
    if (*gaps) c = '"';
    else {
      c = ' ';
      gaps = "0";
    }
    printf("#undef OPTSTR_%s\n#define OPTSTR_%s %c%s%c\n",
            command, command, c, gaps, c);
    free(mgaps);

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
      if (aflist->lopt) {
        if (flist && flist->lopt &&
            !strcmp(flist->lopt->command, aflist->lopt->command))
        {
          sprintf(out, "#define FLAG_%s (1<<%d)\n", flist->lopt->command, bit);
          flist->lopt = flist->lopt->next;
        } else sprintf(out, "#define FLAG_%s (FORCED_FLAG<<%d)\n",
                       aflist->lopt->command, bit);
        aflist->lopt = aflist->lopt->next;
        if (!aflist->command) {
          aflist = aflist->next;
          bit++;
          if (flist) flist = flist->next;
        }
      } else if (aflist->command) {
        if (flist && (!flist->command || *aflist->command == *flist->command)) {
          if (aflist->command)
            sprintf(out, "#define FLAG_%c (1<<%d)\n", *aflist->command, bit);
          flist = flist->next;
        } else sprintf(out, "#define FLAG_%c (FORCED_FLAG<<%d)\n",
                       *aflist->command, bit);
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
