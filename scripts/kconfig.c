#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
mainmenu, source, comment
config
  bool "string"
  int "string"
  default y/n/SYMBOL
  depends [on] SYMBOL
  help
menu/choice
  prompt "string"
  default, help
  [config...]
endmenu/endchoice

# bool without a string is invisible, no prompt just help text
*/

// Data we're collecting about each entry
struct kconfig {
  struct kconfig *next;
  char *symbol, *value, *type, *prompt, *def, *depend, *help;
};

// Skip/remove leading space, quotes, and escapes within quotes
char *trim(char *s)
{
  int len, in, out;

  while (isspace(*s)) s++;
  len = strlen(s);
  if (*s=='\"' && s[len-1]=='\"') {
    s[--len]=0;
    s++;
    for (in = out = 0; in<len; in++) {
      if (s[in]=='\\') in++;
      s[out++] = s[in];
    }
    s[out] = 0;
  }

  return s;
}

// Check string against 0 terminated array of strings, return 0 if no match
char *strany(char *needle, char *haystack[])
{
  int ii;

  for (ii = 0; haystack[ii] && strcmp(needle, haystack[ii]); ii++);

  return haystack[ii];
}

// Free *to, assign *to = *from, and zero *from.
void bump(char **to, char **from)
{
  free(*to);
  *to = *from;
  *from = 0;
}

// Read Config.in file, recursing into "source" lines
struct kconfig *walter(char *name)
{
  FILE *fp = fopen(name, "r");
  char *line = 0, *help = 0, *s, *ss;
  struct kconfig *kc, *klist = 0;
  int ii, jj, count = 0, hindent;
  size_t size = 0;

  if (!fp) dprintf(2, "Can't open '%s'\n", name), exit(1);
  while (0<getline(&line, &size, fp)) {
    count++;

    // Trim trailing whitespace
    for (s = line+strlen(line); --s>=line;) {
      if (!isspace(*s)) break;
      *s = 0;
    }

    // Append help text?
    s = line;
    if (help) {
      if (!kc) { fp = 0; break; }
      for (ii = 0; *s==' ' || *s=='\t'; s++) {
        if (*s=='\t') ii |= 7;
        ii++;
        if (*help && hindent<ii) break;
      }
      if (!*help) {
        if (!*s) continue;
        hindent = ii;
      }

      // Does this end the help block?
      jj = strlen(help);
      if (ii<hindent && *s) {
        for (ii = strlen(help); ii--; help[ii]=0) if (!isspace(help[ii])) break;
        kc->help = help;
        help = 0;
      } else {
        help = realloc(help, jj+strlen(s)+2);
        if (jj) help[jj++]='\n';
        strcpy(help+jj, s);

        continue;
      }
    }

    // What's the keyword?
    while (isspace(*s)) s++;
    if (!*s || *s=='#') continue;
    ss = s;
    while (*s && !isspace(*s)) s++;
    ss = strndup(ss, s-ss);

    // Include other file
    if (!strcmp(ss, "source")) {
      struct kconfig *kt;

      if (!(kt = walter(trim(s)))) { fp = 0; break; }
      if (klist) kc = (kc->next) = kt;
      else klist = kc = kt;
      while (kc->next) kc = kc->next;
    // start help block
    } else if (!strcmp(ss, "help")) help = strdup("");
    // start a new config entry?
    else if (strany(ss, (char *[]){"mainmenu", "menu", "comment", "config",
        "menu", "choice", "endmenu", "endchoice", 0}))
    {
      struct kconfig *kt = calloc(sizeof(struct kconfig), 1);

      if (klist) kc = (kc->next = kt);
      else klist = kc = kt;
      if (!strcmp(ss, "config")) {
        if (!*s) { fp = 0; break; }
        else kt->symbol = strdup(trim(s));
      } else if (*s) kt->prompt = strdup(trim(s));
      bump(&kt->type, &ss);
    } else if (!kc) { fp = 0; break; }
    else if (!strcmp(ss, "default")) bump(&kc->def, &ss);
    else if (strany(ss, (char *[]){"bool", "int", "prompt", 0})) {
      if (*ss!='p') bump(&kc->type, &ss);
      ss = strdup(trim(s));
      bump(&kc->prompt, &ss);
    } else if (!strcmp(ss, "depends")) {
      while (isspace(*s)) s++;
      if (s[0]=='o' && s[1]=='n' && isspace(s[2])) s += 3;
      ss = strdup(trim(s));
      bump(&kc->depend, &ss);
    } else { dprintf(2, "%d %s\n", count, line); fp = 0; break; }

    free(ss);
  }

  if (!fp) dprintf(2, "%s[%d]: bad %s\n", name, count, line ? : ""), exit(1);
  fclose(fp);

  return klist;
}

int main(int argc, char *argv[])
{
  struct kconfig *kc = walter("Config.in"), *kk;
  char *ss, *tt, *esc = "\n\\\"";

  if (argc==2 && !strcmp(argv[1], "-h")) for (kk = kc; kk; kk = kk->next) {
    if (!kk->symbol || !kk->help) continue;
    printf("#define HELP_");
    for (ss = kk->symbol; *ss; ss++) putchar(tolower(*ss));
    printf(" \"");
    for (ss = kk->help; *ss; ss++)
      if (!(tt = strchr(esc, *ss))) putchar(*ss);
      else printf("\\%c", "n\\\""[tt-esc]);
    printf("\"\n\n");
  }

  return 0;
}
