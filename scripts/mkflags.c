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

// Break down a command string into struct flag list.

struct flag *digest(char *string)
{
  struct flag *list = NULL;

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

  for (;;) {
    struct flag *flist, *aflist, *offlist;
    unsigned bit;

    *command = 0;
    bit = fscanf(stdin, "%255s \"%1023[^\"]\" \"%1023[^\"]\"\n",
                    command, flags, allflags);

    if (!*command) break;
    if (bit != 3) {
      fprintf(stderr, "\nError in %s (duplicate command?)\n", command);
      exit(1);
    }

    bit = 0;
    printf("// %s %s %s\n", command, flags, allflags);

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
        } else sprintf(out, "#define FLAG_%s 0\n", aflist->lopt->command);
        aflist->lopt = aflist->lopt->next;
        if (!aflist->command) {
          aflist = aflist->next;
          if (flist) {
            flist = flist->next;
            bit++;
          }
        }
      } else if (aflist->command) {
        if (flist && (!aflist->command || *aflist->command == *flist->command))
        {
          if (aflist->command)
            sprintf(out, "#define FLAG_%c (1<<%d)\n", *aflist->command, bit);
          bit++;
          flist = flist->next;
        } else sprintf(out, "#define FLAG_%c 0\n", *aflist->command);
        aflist = aflist->next;
      }
      out += strlen(out);
    }
    sprintf(out, "#endif\n\n");
    out += strlen(out);
  }

  if (fflush(0) && ferror(stdout)) return 1;

  out = outbuf;
  while (*out) {
    int i = write(1, outbuf, strlen(outbuf));

    if (i<0) return 1;
    out += i;
  }
}
