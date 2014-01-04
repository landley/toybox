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
  struct symbol *try;
  char *s, *file;

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
      s = line+7;
      for (try=sym; try; try=try->next) {
        len = strlen(try->name);
        if (!strncmp(try->name, s, len) && s[len]=='=' && s[len+1]=='y') {
          try->enabled++;
          break;
        } 
      }
    }
  }

  // Print out help #defines
  while (sym) {
    struct double_list *dd;

    if (sym->help) {
      int i, padlen = 0;

      s = xstrdup(sym->name);
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
        dd = dd->next;
        if (dd == sym->help) break;
      }
      printf("\"\n");
    }
    sym = sym->next;
  }
}
