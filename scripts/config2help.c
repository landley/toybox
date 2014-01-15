#include "toys.h"

// Humor toys.h
struct toy_context toys;
char libbuf[4096], toybuf[4096];
void show_help(void) {;}
void toy_exec(char *argv[]) {;}

// Parse config files into data structures.

struct symbol {
  struct symbol *next;
  int enabled;
  char *name, *depends;
  struct double_list *help;
} *sym;

char *keyword(char *name, char *line)
{
  int len = strlen(name);

  while (isspace(*line)) line++;
  if (strncmp(name, line, len)) return 0;
  line += len;
  if (*line && !isspace(*line)) return 0;
  while (isspace(*line)) line++;

  return line;
}

void parse(char *filename)
{
  FILE *fp = xfopen(filename, "r");
  struct symbol *new = 0;
  int help = 0;

  for (;;) {
    char *s, *line = NULL;
    size_t len;

    // Read line, trim whitespace at right edge.
    if (getline(&line, &len, fp) < 1) break;
    s = line+strlen(line);
    while (--s >= line) {
      if (!isspace(*s)) break;
      *s = 0;
    }

    // source or config keyword at left edge?
    if (*line && !isspace(*line)) {
      help = 0;
      if ((s = keyword("config", line))) {
        new = xzalloc(sizeof(struct symbol));
        new->next = sym;
        new->name = s;
        sym = new;
      } else if ((s = keyword("source", line))) parse(s);

      continue;
    }
    if (!new) continue;

    if (help) dlist_add(&(new->help), line);
    else if ((s = keyword("depends", line)) && (s = keyword("on", s)))
      new->depends = s;
    else if (keyword("help", line)) help++;
  }

  fclose(fp);
}

int main(int argc, char *argv[])
{
  FILE *fp;
  char *file;

  if (argc != 3) {
    fprintf(stderr, "usage: config2help Config.in .config\n");
    exit(1);
  }

  // Read Config.in
  parse(argv[1]);

  // read .config
  fp = xfopen(argv[2], "r");
  for (;;) {
    char *line = NULL;
    size_t len;

    if (getline(&line, &len, fp) < 1) break;
    if (!strncmp("CONFIG_", line, 7)) {
      struct symbol *try;
      char *s = line+7;

      for (try=sym; try; try=try->next) {
        len = strlen(try->name);
        if (!strncmp(try->name, s, len) && s[len]=='=' && s[len+1]=='y') {
          try->enabled++;
          break;
        } 
      }
    }
  }

  // Collate help according to usage, depends, and .config

  // Loop through each entry, finding duplicate enabled "usage:" names

  for (;;) {
    struct symbol *throw = 0, *catch;
    char *this, *that, *name;
    int len;

    // find a usage: name and collate all enabled entries with that name
    for (catch = sym; catch; catch = catch->next) {
      if (catch->enabled != 1) continue;
      if (catch->help && (this = keyword("usage:", catch->help->data))) {
        struct double_list *bang;

        if (!throw) {
          throw = catch;
          catch->enabled++;
          name = this;
          while (!isspace(*this) && *this) this++;
          len = (that = this)-name;
          while (isspace(*that)) that++;

          continue;
        }

        if (strncmp(name, this, len) || !isspace(this[len])) continue;
        catch->enabled++;

        // Suck help text out of throw into catch.
        throw->enabled = 0;

        // splice together circularly linked lists
        bang = throw->help->prev;
        throw->help->prev->next = catch->help;
        throw->help->prev = catch->help->prev;
        catch->help->prev->next = throw->help;
        catch->help->prev = bang;
        throw->help = 0;
        throw = catch;
      }
    }

    // Did we find one?

    if (!throw) break;

      // Collate first [-abc] option block?

//      if (*s == '[' && s[1] == '-' && s[2] != '-') {
//      }
  }

  // Print out help #defines
  while (sym) {
    struct double_list *dd;

    if (sym->help) {
      int i, padlen = 0;
      char *s = xstrdup(sym->name);

      for (i = 0; s[i]; i++) s[i] = tolower(s[i]);
      printf("#define help_%s \"", s);
      free(s);

      // Measure leading whitespace of first line
      dd = sym->help;
      while (isspace(dd->data[padlen])) padlen++;

      for (;;) {
        i = padlen;

        // Trim leading whitespace
        s = dd->data;
        while (isspace(*s) && i) {
          s++;
          i--;
        }
        for (i=0; s[i]; i++) {
          if (s[i] == '"' || s[i] == '\\') putchar('\\');
          putchar(s[i]);
        }
        putchar('\\');
        putchar('n');
        dd = dd->next;
        if (dd == sym->help) break;
      }
      printf("\"\n");
    }
    sym = sym->next;
  }
}
