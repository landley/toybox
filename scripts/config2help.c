#include "toys.h"

// Humor toys.h
struct toy_context toys;
char libbuf[4096], toybuf[4096];
void show_help(void) {;}
void toy_exec(char *argv[]) {;}

// Parse config files into data structures.

struct symbol {
  struct symbol *next;
  int enabled, help_indent;
  char *name, *depends;
  struct double_list *help;
} *sym;

char *trim(char *s)
{
  while (isspace(*s)) s++;

  return s;
}

char *keyword(char *name, char *line)
{
  int len = strlen(name);

  line = trim(line);
  if (strncmp(name, line, len)) return 0;
  line += len;
  if (*line && !isspace(*line)) return 0;
  line = trim(line);

  return line;
}

char *dlist_zap(struct double_list **help)
{
  struct double_list *dd = dlist_pop(help);
  char *s = dd->data;

  free(dd);
  return s;
}

void zap_blank_lines(struct double_list **help)
{
  for(;;) {
    char *s = trim((*help)->data);;

    if (*s) break;
    free(dlist_zap(help));
  }
}

void parse(char *filename)
{
  FILE *fp = xfopen(filename, "r");
  struct symbol *new = 0;

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
      if ((s = keyword("config", line))) {
        new = xzalloc(sizeof(struct symbol));
        new->next = sym;
        new->name = s;
        sym = new;
      } else if ((s = keyword("source", line))) parse(s);

      continue;
    }
    if (!new) continue;

    if (sym && sym->help_indent) {
      dlist_add(&(new->help), line);
      if (sym->help_indent < 0) {
        sym->help_indent = 0;
        while (isspace(line[sym->help_indent])) sym->help_indent++;
      }
    }
    else if ((s = keyword("depends", line)) && (s = keyword("on", s)))
      new->depends = s;
    else if (keyword("help", line)) sym->help_indent = -1;
  }

  fclose(fp);
}

int charsort(void *a, void *b)
{
  char *aa = a, *bb = b;

  if (*aa < *bb) return -1;
  if (*aa > *bb) return 1;
  return 0;
}

int dashsort(void *a, void *b)
{
  char *aa = *(char **)a, *bb = *(char **)b;

  if (aa[1] < bb[1]) return -1;
  if (aa[1] > bb[1]) return 1;
  return 0;
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
    char *this, *that, *cusage, *tusage, *name;
    int len;

    // find a usage: name and collate all enabled entries with that name
    for (catch = sym; catch; catch = catch->next) {
      if (catch->enabled != 1) continue;
      if (catch->help && (that = keyword("usage:", catch->help->data))) {
        struct double_list *bang;
        char *try;

        // Suck help text out of throw into catch, copying from this to that

        if (!throw) name = that;
        else if (strncmp(name, that, len) || !isspace(that[len])) continue;
        catch->enabled++;
        while (!isspace(*that) && *that) that++;
        if (!throw) len = that-name;
        that = trim(that);
        if (!throw) {
          throw = catch;
          this = that;

          continue;
        }

        // Grab usage: lines to collate
        cusage = dlist_zap(&catch->help);
        zap_blank_lines(&catch->help);
        tusage = dlist_zap(&throw->help);
        zap_blank_lines(&throw->help);

        // Collate first [-abc] option block

        try = 0;
        if (*this == '[' && this[1] == '-' && this[2] != '-' &&
            *that == '[' && that[1] == '-' && that[2] != '-')
        {
          char *from = this+2, *to = that+2;
          int ff = strcspn(from, " ]"), tt = strcspn(to, " ]");

          if (from[ff] == ']' && to[tt] == ']') {
            try = xmprintf("[-%.*s%.*s] ", ff, from, tt, to);
            qsort(try+2, ff+tt, 1, (void *)charsort);
            this = trim(this+ff+3);
            that = trim(that+tt+3);
          }
        }

        // Add new collated line (and whitespace).
        dlist_add(&catch->help, xmprintf("%*cusage: %.*s %s%s%s%s",
                  catch->help_indent, ' ', len, name, try ? try : "",
                  this, *this ? " " : "", that));
        dlist_add(&catch->help, strdup(""));
        catch->help = catch->help->prev->prev;

        free(cusage);
        free(tusage);
        free(try);

        throw->enabled = 0;

        // splice together circularly linked lists
        bang = throw->help->prev;
        throw->help->prev->next = catch->help;
        throw->help->prev = catch->help->prev;
        catch->help->prev->next = throw->help;
        catch->help->prev = bang;

        throw->help = 0;
        throw = catch;
        this = throw->help->data + throw->help_indent + 8 + len;
      }
    }

    // Did we find one?

    if (!throw) break;
  }

  // Print out help #defines
  while (sym) {
    struct double_list *dd;

    if (sym->help) {
      int i;
      char *s = xstrdup(sym->name);

      for (i = 0; s[i]; i++) s[i] = tolower(s[i]);
      printf("#define help_%s \"", s);
      free(s);

      dd = sym->help;
      for (;;) {
        i = sym->help_indent;

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
